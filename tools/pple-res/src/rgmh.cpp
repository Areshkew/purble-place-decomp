#include "pple/rgmh.hpp"

#include <cstring>

namespace pple {
namespace {

constexpr std::uint32_t kMagic = 0x484D4752u;  // "RGMH"
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kHeaderSize = 8232;
constexpr std::size_t kProjectNameOffset = 0x28;
constexpr std::size_t kFileNameOffset = 0x828;
constexpr std::size_t kNameCapacity = 0x3FF;  // wcscpy_s sizeInWords

std::uint32_t rd32(std::span<const std::uint8_t> b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) | (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

void wr32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o + 0] = static_cast<std::uint8_t>(v & 0xFF);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// Reads a NUL-terminated UTF-16LE string that must stay inside [offset, limit).
std::u16string read_wide(std::span<const std::uint8_t> d, std::size_t offset, std::size_t limit) {
    std::u16string out;
    for (std::size_t i = offset; i + 1 < limit; i += 2) {
        const char16_t c = static_cast<char16_t>(d[i] | (static_cast<std::uint16_t>(d[i + 1]) << 8));
        if (c == 0) break;
        out.push_back(c);
    }
    return out;
}

bool write_wide(std::vector<std::uint8_t>& b, std::size_t offset, std::size_t limit,
                const std::u16string& text, std::string& error) {
    if (text.size() + 1 > kNameCapacity) {
        error = "name is longer than the 0x3FF wide characters the writer copies";
        return false;
    }
    if (offset + 2 * (text.size() + 1) > limit) {
        error = "name does not fit before the next field";
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        b[offset + 2 * i + 0] = static_cast<std::uint8_t>(text[i] & 0xFF);
        b[offset + 2 * i + 1] = static_cast<std::uint8_t>((text[i] >> 8) & 0xFF);
    }
    b[offset + 2 * text.size()] = 0;
    b[offset + 2 * text.size() + 1] = 0;
    return true;
}

}  // namespace

std::size_t first_difference(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return i;
    return a.size() == b.size() ? std::string::npos : n;
}

bool rgmh_read(std::span<const std::uint8_t> data, RgmhFile& out, std::string& error) {
    out = RgmhFile{};
    if (data.size() < 0x28) {
        error = "shorter than the RGMH header prefix";
        return false;
    }
    if (rd32(data, 0) != kMagic) {
        error = "magic is not RGMH";
        return false;
    }
    const std::uint32_t version = rd32(data, 4);
    std::uint32_t header_size = rd32(data, 8);
    if (header_size == 8240) {
        // Legacy layout: the reader shifts bytes +16.. down to +12 and re-reads the file
        // position, so the extra 8 bytes live between the GUID and the names.
        error = "8240-byte legacy header is not supported by this reader (the engine shifts it by 8)";
        return false;
    }
    if (version != kVersion) {
        error = "version is not 1";
        return false;
    }
    if (header_size != kHeaderSize) {
        error = "header size is not 8232";
        return false;
    }
    if (data.size() < header_size) {
        error = "file is shorter than the header it declares";
        return false;
    }

    out.version = version;
    out.header_size = header_size;
    out.field_0x0C = rd32(data, 0x0C);
    out.field_0x10 = rd32(data, 0x10);
    out.thumbnail.assign(0, 0);

    const std::uint32_t thumbnail_size = rd32(data, 0x14);
    std::memcpy(out.guid.data(), data.data() + 0x18, 16);
    out.project_name = read_wide(data, kProjectNameOffset, kFileNameOffset);
    out.file_name = read_wide(data, kFileNameOffset, header_size);

    const std::size_t thumb_off = header_size;
    if (thumb_off + thumbnail_size > data.size()) {
        error = "thumbnail size runs past end of file";
        return false;
    }
    out.thumbnail.assign(data.begin() + static_cast<std::ptrdiff_t>(thumb_off),
                         data.begin() + static_cast<std::ptrdiff_t>(thumb_off + thumbnail_size));

    const std::size_t trailer = thumb_off + thumbnail_size;
    if (trailer + 6 > data.size()) {
        error = "no room for the length field and BOM after the thumbnail";
        return false;
    }
    out.xml_length = rd32(data, trailer);
    if (data[trailer + 4] != 0xFF || data[trailer + 5] != 0xFE) {
        error = "UTF-16LE BOM missing after the length field";
        return false;
    }
    out.xml.assign(data.begin() + static_cast<std::ptrdiff_t>(trailer + 4), data.end());
    if (out.xml_length != out.xml.size()) {
        error = "length field is " + std::to_string(out.xml_length) + " but BOM + payload is " +
                std::to_string(out.xml.size()) + " bytes";
        return false;
    }
    return true;
}

std::vector<std::uint8_t> rgmh_write(const RgmhFile& file) {
    std::vector<std::uint8_t> out(file.header_size, 0);
    wr32(out, 0, kMagic);
    wr32(out, 4, file.version);
    wr32(out, 8, file.header_size);
    wr32(out, 0x0C, file.field_0x0C);
    wr32(out, 0x10, file.field_0x10);
    wr32(out, 0x14, static_cast<std::uint32_t>(file.thumbnail.size()));
    std::memcpy(out.data() + 0x18, file.guid.data(), 16);
    std::string ignored;
    write_wide(out, kProjectNameOffset, kFileNameOffset, file.project_name, ignored);
    write_wide(out, kFileNameOffset, file.header_size, file.file_name, ignored);

    out.insert(out.end(), file.thumbnail.begin(), file.thumbnail.end());
    const std::uint32_t length =
        file.xml_length != 0 ? file.xml_length : static_cast<std::uint32_t>(file.xml.size());
    const std::size_t at = out.size();
    out.resize(at + 4);
    wr32(out, at, length);
    out.insert(out.end(), file.xml.begin(), file.xml.end());
    return out;
}

}  // namespace pple
