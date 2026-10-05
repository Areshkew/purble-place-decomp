// PNG decoder built on the DEFLATE decoder in inflate.hpp.
//
// Scope: non-interlaced PNG, bit depths 1/2/4/8/16, colour types 0/2/3/4/6, tRNS for
// palette and grey.  Everything is expanded to 8-bit RGBA, which is byte-for-byte
// SDL_PIXELFORMAT_RGBA32 on a little-endian host.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pple {

struct PngImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int bit_depth = 0;
    int colour_type = 0;  // 0 grey, 2 RGB, 3 palette, 4 grey+alpha, 6 RGBA
    bool has_alpha = false;
    bool from_palette = false;
    std::vector<std::uint8_t> rgba;  // width*height*4, R,G,B,A

    std::string colour_type_name() const;
};

// Decodes a whole PNG file image (signature included).
bool png_decode(std::span<const std::uint8_t> data, PngImage& out, std::string& error);

// Reads only the IHDR, for cheap identification.
bool png_probe(std::span<const std::uint8_t> data, std::uint32_t& width, std::uint32_t& height,
               int& bit_depth, int& colour_type, bool& interlaced);

}  // namespace pple
