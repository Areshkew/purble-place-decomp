// PNG encoder: the counterpart of png.cpp's decoder, used by the savegame's thumbnail.
//
// The shipped game stores that thumbnail as PNG bytes in the RGMH container's thumbnail block
// (WriteSpecialFolderFileRichGameHeader 0x1000A1D18 takes `*(u32 *)thumbnail` as its size, so
// the bytes are whatever the host handed it).  Writing one here needs no compression: the file
// is a single stored DEFLATE stream, which libpng and every decoder accept, and the game's own
// reader only seeks past the block.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pple {

// Encodes 8-bit RGBA rows into a complete PNG file image (signature included).  `rgba` must be
// width * height * 4 bytes.  The result is an 8-bit RGBA (colour type 6) PNG.
bool png_write(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height,
               std::vector<std::uint8_t>& out, std::string& error);

}  // namespace pple
