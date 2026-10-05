// The "RGMH" save-file container.
//
// Written by WriteSpecialFolderFileRichGameHeader (PurblePlace.exe 0x1000A1D18), read by
// ReadSpecialFolderFileRichGameHeader (0x1000A1E30).  Both were reversed, so every field
// below is a reading of the code rather than an inference from one sample.
//
// The writer allocates 0x2028 (8232) bytes, memsets them to zero, fills these fields and
// writes all 8232 bytes, then writes the thumbnail separately:
//
//   +0x00  u32  magic            0x484D4752 ("RGMH")
//   +0x04  u32  version          1
//   +0x08  u32  header_size      8232, a constant
//   +0x0C  u32  zeroed ("*(_QWORD *)(v9 + 3) = 0")
//   +0x10  u32  zeroed (same store; only set when there is no thumbnail)
//   +0x14  u32  thumbnail_size   = *(u32 *)thumbnail
//   +0x18  16   GUID             copied from the caller
//   +0x28  ...  project name     wcscpy_s((wchar_t *)v9 + 20, 0x3FF, name)
//   +0x828 ...  file name        wcscpy_s((wchar_t *)v9 + 1044, 0x3FF, name)
//   ...         zero padding to 8232
//   +8232  ...  thumbnail        thumbnail_size bytes
//
// XmlStash::SaveXMLFile (0x100079A00) then writes three more chunks:
//   u32  length      = 2 * characters + 2, i.e. the byte count of the BOM plus the XML
//   u16  0xFEFF      little-endian BOM ("FF FE" on disk)
//   ...  the XML, UTF-16LE, to end of file
//
// The reader validates magic, version, header_size == 8232 and then seeks past
// thumbnail_size, which is what fixes the trailer at 8232 + thumbnail_size.  It also accepts
// a legacy 8240-byte header by shifting the block down by 8 bytes before the same checks.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pple {

struct RgmhFile {
    std::uint32_t version = 1;
    std::uint32_t header_size = 8232;
    std::uint32_t field_0x0C = 0;  // zeroed by the writer; purpose not established
    std::uint32_t field_0x10 = 0;  // zeroed by the writer; purpose not established
    std::array<std::uint8_t, 16> guid{};
    std::u16string project_name;   // stored at 0x28, NUL-terminated, at most 0x3FF units
    std::u16string file_name;      // stored at 0x828, NUL-terminated, at most 0x3FF units
    std::vector<std::uint8_t> thumbnail;  // PNG bytes, thumbnail_size of them
    std::uint32_t xml_length = 0;         // must equal xml.size()
    std::vector<std::uint8_t> xml;        // the FF FE BOM followed by UTF-16LE XML

    std::size_t thumbnail_offset() const { return header_size; }
    std::size_t trailer_offset() const { return header_size + thumbnail.size(); }
};

// Returns false with a message for anything the reader itself would reject.
bool rgmh_read(std::span<const std::uint8_t> data, RgmhFile& out, std::string& error);

// Re-emits the container.  The header block is rebuilt from the fields the writer fills, so a
// successful round trip is evidence that nothing else is stored in it.
std::vector<std::uint8_t> rgmh_write(const RgmhFile& file);

// Index of the first differing byte, or npos when the two buffers are equal.
std::size_t first_difference(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b);

}  // namespace pple
