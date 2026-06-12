#include "tiff_reader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace uhdrtool {
namespace {

// ---- file slurp -------------------------------------------------------------

bool readWholeFile(const std::string& path, std::vector<uint8_t>& bytes,
                   std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open '" + path + "'";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    if (size <= 0) {
        std::fclose(f);
        error = "'" + path + "' is empty or unreadable";
        return false;
    }
    std::fseek(f, 0, SEEK_SET);
    bytes.resize(static_cast<size_t>(size));
    size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got != bytes.size()) {
        error = "short read on '" + path + "'";
        return false;
    }
    return true;
}

// ---- endian-aware little readers over an in-memory buffer -------------------

struct Reader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool big = false;  // byte order: MM (big-endian) vs II (little-endian)

    bool u16(size_t off, uint16_t& v) const {
        if (off + 2 > size) return false;
        v = big ? static_cast<uint16_t>((data[off] << 8) | data[off + 1])
                : static_cast<uint16_t>((data[off + 1] << 8) | data[off]);
        return true;
    }
    bool u32(size_t off, uint32_t& v) const {
        if (off + 4 > size) return false;
        if (big) {
            v = (uint32_t(data[off]) << 24) | (uint32_t(data[off + 1]) << 16) |
                (uint32_t(data[off + 2]) << 8) | uint32_t(data[off + 3]);
        } else {
            v = (uint32_t(data[off + 3]) << 24) | (uint32_t(data[off + 2]) << 16) |
                (uint32_t(data[off + 1]) << 8) | uint32_t(data[off]);
        }
        return true;
    }
    bool f32(size_t off, float& v) const {
        uint32_t bits;
        if (!u32(off, bits)) return false;
        std::memcpy(&v, &bits, 4);
        return true;
    }
};

// A single IFD entry, with its scalar values already resolved into u32s
// (StripOffsets/ByteCounts can be SHORT or LONG arrays; we promote both).
constexpr uint16_t kTypeShort = 3;
constexpr uint16_t kTypeLong = 4;

size_t typeSize(uint16_t type) {
    switch (type) {
        case 1:           // BYTE
        case 2:           // ASCII
        case 6:           // SBYTE
        case 7: return 1;  // UNDEFINED
        case 3:           // SHORT
        case 8: return 2;  // SSHORT
        case 4:           // LONG
        case 9:           // SLONG
        case 11: return 4; // FLOAT
        case 5:           // RATIONAL
        case 10:          // SRATIONAL
        case 12: return 8; // DOUBLE
        default: return 0;
    }
}

// Read all `count` values of an integer-typed (SHORT/LONG) tag into `out`.
bool readIntArray(const Reader& r, uint16_t type, uint32_t count,
                  size_t valueFieldOff, std::vector<uint32_t>& out) {
    size_t ts = typeSize(type);
    if (ts == 0 || (type != kTypeShort && type != kTypeLong)) return false;
    size_t total = ts * count;
    size_t base = valueFieldOff;
    if (total > 4) {  // values stored out-of-line at the offset in the value field
        uint32_t off;
        if (!r.u32(valueFieldOff, off)) return false;
        base = off;
    }
    out.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (type == kTypeShort) {
            uint16_t v;
            if (!r.u16(base + i * 2, v)) return false;
            out[i] = v;
        } else {
            uint32_t v;
            if (!r.u32(base + i * 4, v)) return false;
            out[i] = v;
        }
    }
    return true;
}

// ---- IEEE-754 float32 -> float16, round-to-nearest-even (matches numpy) -----

uint16_t floatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000u;
    uint32_t expBits = (x >> 23) & 0xffu;
    uint32_t mant = x & 0x7fffffu;

    if (expBits == 0xff) {                       // Inf / NaN
        return static_cast<uint16_t>(sign | (mant ? 0x7e00u : 0x7c00u));
    }
    int32_t exp = static_cast<int32_t>(expBits) - 127 + 15;
    if (exp >= 0x1f) {                           // overflow -> Inf
        return static_cast<uint16_t>(sign | 0x7c00u);
    }
    if (exp <= 0) {                              // subnormal or zero
        if (exp < -10) return static_cast<uint16_t>(sign);
        mant |= 0x800000u;                       // restore implicit leading 1
        uint32_t shift = static_cast<uint32_t>(14 - exp);
        uint32_t half = mant >> shift;
        uint32_t rem = mant & ((1u << shift) - 1);
        uint32_t halfway = 1u << (shift - 1);
        if (rem > halfway || (rem == halfway && (half & 1u))) ++half;
        return static_cast<uint16_t>(sign | half);
    }
    uint16_t half = static_cast<uint16_t>(sign | (uint32_t(exp) << 10) | (mant >> 13));
    uint32_t rem = mant & 0x1fffu;               // 13 dropped bits
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) ++half;  // carry ripples into exp
    return half;
}

constexpr uint16_t kHalfOne = 0x3c00;  // 1.0 in IEEE half

}  // namespace

bool readDirectHdrTiff(const std::string& path, HdrImage& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!readWholeFile(path, bytes, error)) return false;

    Reader r;
    r.data = bytes.data();
    r.size = bytes.size();

    if (r.size < 8) { error = "not a TIFF (file too small)"; return false; }
    if (bytes[0] == 'I' && bytes[1] == 'I') r.big = false;
    else if (bytes[0] == 'M' && bytes[1] == 'M') r.big = true;
    else { error = "not a TIFF (bad byte-order mark)"; return false; }

    uint16_t magic;
    if (!r.u16(2, magic) || magic != 42) { error = "not a TIFF (bad magic)"; return false; }

    uint32_t ifdOff;
    if (!r.u32(4, ifdOff)) { error = "truncated TIFF (no IFD offset)"; return false; }

    uint16_t numEntries;
    if (!r.u16(ifdOff, numEntries)) { error = "truncated TIFF (no IFD)"; return false; }

    // Tags we care about.
    uint32_t width = 0, height = 0;
    uint32_t compression = 1;       // default: none
    uint32_t samplesPerPixel = 1;
    uint32_t planarConfig = 1;      // default: chunky
    uint32_t rowsPerStrip = 0xffffffffu;
    bool haveSubIFD = false;
    bool sampleFormatFloat = false;
    bool sawSampleFormat = false;
    std::vector<uint32_t> bitsPerSample;
    std::vector<uint32_t> stripOffsets;
    std::vector<uint32_t> stripByteCounts;

    for (uint16_t e = 0; e < numEntries; ++e) {
        size_t entryOff = static_cast<size_t>(ifdOff) + 2 + size_t(e) * 12;
        uint16_t tag, type;
        uint32_t count;
        if (!r.u16(entryOff, tag) || !r.u16(entryOff + 2, type) ||
            !r.u32(entryOff + 4, count)) {
            error = "corrupt IFD entry";
            return false;
        }
        size_t valueField = entryOff + 8;

        auto scalar = [&](uint32_t& dst) {
            std::vector<uint32_t> v;
            if (readIntArray(r, type, count, valueField, v) && !v.empty()) dst = v[0];
        };

        switch (tag) {
            case 256: scalar(width); break;
            case 257: scalar(height); break;
            case 258: readIntArray(r, type, count, valueField, bitsPerSample); break;
            case 259: scalar(compression); break;
            case 273: readIntArray(r, type, count, valueField, stripOffsets); break;
            case 277: scalar(samplesPerPixel); break;
            case 278: scalar(rowsPerStrip); break;
            case 279: readIntArray(r, type, count, valueField, stripByteCounts); break;
            case 284: scalar(planarConfig); break;
            case 330: haveSubIFD = true; break;  // SubIFD => gain map
            case 339: {
                std::vector<uint32_t> sf;
                if (readIntArray(r, type, count, valueField, sf) && !sf.empty()) {
                    sawSampleFormat = true;
                    sampleFormatFloat = (sf[0] == 3);  // 3 = IEEE float
                }
                break;
            }
            default: break;
        }
    }

    // ---- classify (per the hdr-tiff-reader spec) ----------------------------
    if (haveSubIFD) {
        error = "this TIFF has a SubIFD gain map (\"Maximize Compatibility\" was ON).\n"
                "Re-export with 32-bit float and \"Maximize Compatibility\" OFF.";
        return false;
    }
    if (!sawSampleFormat || !sampleFormatFloat) {
        error = "expected a 32-bit float HDR TIFF; this file is not float.\n"
                "Re-export at 32 bits/component with HDR Output on.";
        return false;
    }
    for (uint32_t b : bitsPerSample) {
        if (b != 32) {
            error = "expected 32 bits/component float; got " + std::to_string(b) + ".\n"
                    "Re-export at 32 bits/component with HDR Output on.";
            return false;
        }
    }
    if (compression != 1) {
        error = "unsupported TIFF compression (" + std::to_string(compression) +
                "); expected uncompressed.";
        return false;
    }
    if (planarConfig != 1) {
        error = "unsupported planar TIFF; expected chunky (interleaved) pixels.";
        return false;
    }
    if (samplesPerPixel != 3 && samplesPerPixel != 4) {
        error = "unsupported sample count (" + std::to_string(samplesPerPixel) +
                "); expected RGB or RGBA.";
        return false;
    }
    if (width == 0 || height == 0) { error = "TIFF has zero dimensions"; return false; }
    if (stripOffsets.empty() || stripOffsets.size() != stripByteCounts.size()) {
        error = "TIFF strip table missing or inconsistent";
        return false;
    }

    // ---- decode uncompressed float32 strips into RGBA half ------------------
    const uint32_t spp = samplesPerPixel;
    const uint64_t expectedSamples = uint64_t(width) * height * spp;
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.rgba_f16.assign(size_t(width) * height * 4, 0);

    uint64_t sampleIdx = 0;  // running index over R,G,B[,A] samples in row-major order
    for (size_t s = 0; s < stripOffsets.size(); ++s) {
        size_t off = stripOffsets[s];
        uint32_t nbytes = stripByteCounts[s];
        if (nbytes % 4 != 0) { error = "strip byte count not float-aligned"; return false; }
        uint32_t nfloats = nbytes / 4;
        if (off + nbytes > r.size) { error = "strip extends past end of file"; return false; }

        for (uint32_t i = 0; i < nfloats && sampleIdx < expectedSamples; ++i, ++sampleIdx) {
            float v;
            r.f32(off + i * 4, v);
            // Match the oracle's np.clip(img, 0, None): clip true negatives to 0,
            // but let NaN and overflow pass through to floatToHalf exactly as
            // numpy's astype(float16) does (real LR exports contain neither).
            if (v < 0.0f) v = 0.0f;
            uint32_t pixel = static_cast<uint32_t>(sampleIdx / spp);
            uint32_t chan = static_cast<uint32_t>(sampleIdx % spp);
            if (chan < 3) out.rgba_f16[size_t(pixel) * 4 + chan] = floatToHalf(v);
            // a 4th input sample (alpha), if any, is dropped; alpha is forced below
        }
    }

    if (sampleIdx != expectedSamples) {
        error = "TIFF pixel data is shorter than its declared dimensions";
        return false;
    }

    // Force alpha = 1.0 on every pixel (libultrahdr ignores it, oracle writes 1.0).
    for (size_t p = 0; p < size_t(width) * height; ++p) {
        out.rgba_f16[p * 4 + 3] = kHalfOne;
    }
    return true;
}

}  // namespace uhdrtool
