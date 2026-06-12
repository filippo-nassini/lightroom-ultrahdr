// Minimal baseline-TIFF reader for Lightroom's *direct* HDR export:
// a single-page, uncompressed, 32-bit IEEE-float RGB TIFF (no gain map).
//
// This is deliberately not a general TIFF library. It parses just enough of the
// first IFD to (a) classify the file and (b) read uncompressed float32 strips,
// producing the interleaved RGBA half-float buffer libultrahdr wants for a
// UHDR_IMG_FMT_64bppRGBAHalfFloat raw image. Gain-mapped or non-float TIFFs are
// rejected with actionable guidance rather than mis-read.
#ifndef UHDRTOOL_TIFF_READER_H
#define UHDRTOOL_TIFF_READER_H

#include <cstdint>
#include <string>
#include <vector>

namespace uhdrtool {

// Decoded direct-HDR image: interleaved RGBA half-float pixels (R, G, B, 1.0),
// row-major, 8 bytes/pixel. `rgba_f16` holds raw IEEE-754 half bit patterns and
// is sized exactly width*height*4.
struct HdrImage {
    int width = 0;
    int height = 0;
    std::vector<uint16_t> rgba_f16;
};

// Read a Lightroom direct-HDR float TIFF into `out`. Returns true on success.
// On failure returns false and fills `error` with a user-facing message
// (including how to produce a supported file when the input is the wrong form).
bool readDirectHdrTiff(const std::string& path, HdrImage& out, std::string& error);

}  // namespace uhdrtool

#endif  // UHDRTOOL_TIFF_READER_H
