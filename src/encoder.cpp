#include "encoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ultrahdr_api.h"

namespace uhdrtool {
namespace {

constexpr double kSdrWhiteNits = 203.0;    // ITU-R BT.2408 reference white
constexpr double kPqMaxNits = 10000.0;     // SMPTE ST 2084 ceiling

// IEEE-754 half -> float. Exact and deterministic; used to recover the linear
// sample values from the encoded f16 buffer for the percentile computation.
float halfToFloat(uint16_t h) {
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1fu;
    uint32_t mant = h & 0x3ffu;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;                         // +/- 0
        } else {                                 // subnormal -> normalize
            exp = 1;
            while ((mant & 0x400u) == 0) { mant <<= 1; --exp; }
            mant &= 0x3ffu;
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
    } else if (exp == 0x1f) {                    // Inf / NaN
        bits = sign | 0x7f800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Round half-to-even at `decimals` places, matching Python's built-in round().
double roundHalfEven(double x, int decimals) {
    double scale = std::pow(10.0, decimals);
    double scaled = x * scale;
    double floorv = std::floor(scaled);
    double diff = scaled - floorv;
    double r;
    if (diff > 0.5)        r = floorv + 1.0;
    else if (diff < 0.5)   r = floorv;
    else                   r = (std::fmod(floorv, 2.0) == 0.0) ? floorv : floorv + 1.0;
    return r / scale;
}

// Read width/height from a JPEG's first SOF marker, without a full decode. This
// is only for an actionable dimension-mismatch check; the bytes still pass
// through to libultrahdr untouched. Returns false if no SOF is found.
bool jpegDimensions(const std::vector<uint8_t>& b, int& w, int& h) {
    if (b.size() < 4 || b[0] != 0xff || b[1] != 0xd8) return false;  // SOI
    size_t i = 2;
    while (i + 9 < b.size()) {
        if (b[i] != 0xff) { ++i; continue; }
        uint8_t marker = b[i + 1];
        if (marker == 0xd8 || marker == 0xd9 || (marker >= 0xd0 && marker <= 0xd7)) {
            i += 2; continue;  // standalone markers, no length
        }
        size_t len = (size_t(b[i + 2]) << 8) | b[i + 3];
        // SOF0..SOF15 except DHT(c4)/JPGn(c8)/DAC(cc) carry frame dimensions.
        if (marker >= 0xc0 && marker <= 0xcf &&
            marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (i + 9 >= b.size()) return false;
            h = (int(b[i + 5]) << 8) | b[i + 6];
            w = (int(b[i + 7]) << 8) | b[i + 8];
            return true;
        }
        i += 2 + len;
    }
    return false;
}

// numpy np.percentile with method='linear' over `values` (modified in place via
// nth_element). q in [0,100]. Assumes values is non-empty and NaN-free.
double percentileLinear(std::vector<float>& values, double q) {
    const size_t n = values.size();
    if (n == 1) return values[0];
    double virt = (q / 100.0) * static_cast<double>(n - 1);
    size_t lo = static_cast<size_t>(std::floor(virt));
    double frac = virt - static_cast<double>(lo);

    std::nth_element(values.begin(), values.begin() + lo, values.end());
    double vlo = values[lo];
    if (frac == 0.0) return vlo;
    // (lo+1)-th order statistic = min of the partition above `lo`.
    double vhi = *std::min_element(values.begin() + lo + 1, values.end());
    return vlo + frac * (vhi - vlo);
}

}  // namespace

Clamps computeClamps(const HdrImage& img, double percentile, double margin) {
    // Population = all R,G,B samples (negatives already clipped to 0 by the
    // reader); alpha is excluded. Mirrors np.clip(img, 0, None) flattened.
    std::vector<float> rgb;
    rgb.reserve(static_cast<size_t>(img.width) * img.height * 3);
    const size_t pixels = static_cast<size_t>(img.width) * img.height;
    for (size_t p = 0; p < pixels; ++p) {
        rgb.push_back(halfToFloat(img.rgba_f16[p * 4 + 0]));
        rgb.push_back(halfToFloat(img.rgba_f16[p * 4 + 1]));
        rgb.push_back(halfToFloat(img.rgba_f16[p * 4 + 2]));
    }

    Clamps c;
    double peak = rgb.empty() ? 1.0 : percentileLinear(rgb, percentile);
    if (peak < 1.0) peak = 1.0;                  // never below SDR white
    c.peak_boost = peak;
    c.stops = std::log2(peak);
    c.K = roundHalfEven(peak * margin, 3);
    double l = std::min(kPqMaxNits, std::max(kSdrWhiteNits, kSdrWhiteNits * peak));
    c.L = static_cast<int>(roundHalfEven(l, 0));
    return c;
}

bool encodeUltraHdr(const HdrImage& hdr, const Clamps& clamps,
                    const std::string& sdrJpegPath, int hdrCgamut, int sdrCgamut,
                    const std::string& outPath, std::string& error) {
    // --- read the SDR JPEG bytes (passed through untouched; never decoded) ---
    std::FILE* jf = std::fopen(sdrJpegPath.c_str(), "rb");
    if (!jf) { error = "cannot open SDR JPEG '" + sdrJpegPath + "'"; return false; }
    std::fseek(jf, 0, SEEK_END);
    long jsize = std::ftell(jf);
    std::fseek(jf, 0, SEEK_SET);
    if (jsize <= 0) { std::fclose(jf); error = "SDR JPEG is empty"; return false; }
    std::vector<uint8_t> jpeg(static_cast<size_t>(jsize));
    size_t jgot = std::fread(jpeg.data(), 1, jpeg.size(), jf);
    std::fclose(jf);
    if (jgot != jpeg.size()) { error = "short read on SDR JPEG"; return false; }

    // Dimension precondition (design E1): the SDR JPEG and HDR buffer come from
    // the same photo and must match. Surface a mismatch as an actionable error
    // rather than letting it fail opaquely inside libultrahdr.
    int sw = 0, sh = 0;
    if (jpegDimensions(jpeg, sw, sh)) {
        if (sw != hdr.width || sh != hdr.height) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "dimension mismatch: SDR JPEG is %dx%d but HDR TIFF is %dx%d; "
                "both renditions must export at the same resolution",
                sw, sh, hdr.width, hdr.height);
            error = buf;
            return false;
        }
    }

    auto gamutFromInt = [](int g) -> uhdr_color_gamut_t {
        switch (g) {
            case 1: return UHDR_CG_DISPLAY_P3;
            case 2: return UHDR_CG_BT_2100;
            default: return UHDR_CG_BT_709;
        }
    };

    // --- HDR raw descriptor (E2): must-set fmt/ct/cg; stride in PIXELS --------
    uhdr_raw_image_t raw;
    std::memset(&raw, 0, sizeof(raw));
    raw.fmt = UHDR_IMG_FMT_64bppRGBAHalfFloat;
    raw.cg = gamutFromInt(hdrCgamut);
    raw.ct = UHDR_CT_LINEAR;
    raw.range = UHDR_CR_FULL_RANGE;
    raw.w = static_cast<unsigned int>(hdr.width);
    raw.h = static_cast<unsigned int>(hdr.height);
    raw.planes[UHDR_PLANE_PACKED] = const_cast<uint16_t*>(hdr.rgba_f16.data());
    raw.stride[UHDR_PLANE_PACKED] = static_cast<unsigned int>(hdr.width);  // pixels, not bytes
    raw.planes[1] = raw.planes[2] = nullptr;
    raw.stride[1] = raw.stride[2] = 0;

    // --- SDR compressed descriptor (E3) --------------------------------------
    uhdr_compressed_image_t sdr;
    std::memset(&sdr, 0, sizeof(sdr));
    sdr.data = jpeg.data();
    sdr.data_sz = jpeg.size();
    sdr.capacity = jpeg.size();
    sdr.cg = gamutFromInt(sdrCgamut);  // 0 sRGB(BT.709) / 1 P3 / 2 Rec.2020(BT.2100)
    sdr.ct = UHDR_CT_UNSPECIFIED;   // JPEG carries its own transfer
    sdr.range = UHDR_CR_UNSPECIFIED;

    // --- encode sequence (E4) ------------------------------------------------
    uhdr_codec_private_t* enc = uhdr_create_encoder();
    if (!enc) { error = "uhdr_create_encoder failed"; return false; }

    auto fail = [&](const char* where, const uhdr_error_info_t& e) {
        char buf[320];
        std::snprintf(buf, sizeof(buf), "%s failed (code %d)%s%s", where,
                      static_cast<int>(e.error_code), e.has_detail ? ": " : "",
                      e.has_detail ? e.detail : "");
        error = buf;
        uhdr_release_encoder(enc);
        return false;
    };

    uhdr_error_info_t e;
    e = uhdr_enc_set_raw_image(enc, &raw, UHDR_HDR_IMG);
    if (e.error_code != UHDR_CODEC_OK) return fail("set HDR raw image", e);
    e = uhdr_enc_set_compressed_image(enc, &sdr, UHDR_SDR_IMG);
    if (e.error_code != UHDR_CODEC_OK) return fail("set SDR compressed image", e);
    e = uhdr_enc_set_min_max_content_boost(enc, 1.0f, static_cast<float>(clamps.K));
    if (e.error_code != UHDR_CODEC_OK) return fail("set min/max content boost", e);
    e = uhdr_enc_set_target_display_peak_brightness(enc, static_cast<float>(clamps.L));
    if (e.error_code != UHDR_CODEC_OK) return fail("set target display peak brightness", e);
    e = uhdr_encode(enc);
    if (e.error_code != UHDR_CODEC_OK) return fail("encode", e);

    uhdr_compressed_image_t* out = uhdr_get_encoded_stream(enc);
    if (!out || !out->data || out->data_sz == 0) {
        error = "encoder returned an empty stream";
        uhdr_release_encoder(enc);
        return false;
    }

    // --- write the gain-map JPEG ---------------------------------------------
    std::FILE* of = std::fopen(outPath.c_str(), "wb");
    if (!of) { error = "cannot open output '" + outPath + "'"; uhdr_release_encoder(enc); return false; }
    size_t wrote = std::fwrite(out->data, 1, out->data_sz, of);
    std::fclose(of);
    if (wrote != out->data_sz) { error = "short write on output"; uhdr_release_encoder(enc); return false; }

    uhdr_release_encoder(enc);
    return true;
}

}  // namespace uhdrtool
