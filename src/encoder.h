// Clamp computation + libultrahdr "encode scenario 3": fuse the linear
// half-float HDR buffer with the user's SDR JPEG into a gain-map (UltraHDR) JPEG.
//
// The -K/-L clamp math is ported from the Python oracle (hdr_clamps.py) and must
// match it numerically. The encode call sequence is the transcription of the
// uhdrtool-encoder OpenSpec design (E1-E4).
#ifndef UHDRTOOL_ENCODER_H
#define UHDRTOOL_ENCODER_H

#include <cstdint>
#include <string>

#include "tiff_reader.h"

namespace uhdrtool {

// Per-image gain-map clamps derived from the HDR pixels.
struct Clamps {
    double peak_boost = 1.0;  // high-percentile linear value, floored at 1.0
    double stops = 0.0;       // log2(peak_boost)
    double K = 1.0;           // max content boost = peak_boost * margin (3 dp)
    int L = 203;              // target display peak nits = clamp(203*peak, 203, 10000)
};

// Compute -K/-L from the RGBA f16 buffer. `percentile` (default 99.9) and
// `margin` (1.05) are fixed constants, matching the oracle. Mirrors numpy's
// np.percentile(linear-interpolation) over non-negative samples.
Clamps computeClamps(const HdrImage& img, double percentile = 99.9, double margin = 1.05);

// Encode scenario 3. `hdrCgamut`/`sdrCgamut` are the libultrahdr color-gamut
// enum values (0/1/2 and 0/1). Reads `sdrJpegPath` bytes, registers both
// intents, sets K/L, encodes, and writes the gain-map JPEG to `outPath`.
// Returns true on success; on failure fills `error`.
bool encodeUltraHdr(const HdrImage& hdr, const Clamps& clamps,
                    const std::string& sdrJpegPath, int hdrCgamut, int sdrCgamut,
                    const std::string& outPath, std::string& error);

}  // namespace uhdrtool

#endif  // UHDRTOOL_ENCODER_H
