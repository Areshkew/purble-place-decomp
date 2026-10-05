#include "pple/pe_image.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace pple {
namespace {

constexpr std::uint32_t kResourceDirectory = 2;
constexpr std::uint32_t kDebugDirectory = 6;
constexpr std::size_t kMaxNameChars = 512;

std::uint16_t rd16(std::span<const std::uint8_t> b, std::size_t off) {
    return static_cast<std::uint16_t>(b[off] | (static_cast<std::uint16_t>(b[off + 1]) << 8));
}

std::uint32_t rd32(std::span<const std::uint8_t> b, std::size_t off) {
    return static_cast<std::uint32_t>(b[off]) | (static_cast<std::uint32_t>(b[off + 1]) << 8) |
           (static_cast<std::uint32_t>(b[off + 2]) << 16) | (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

std::string to_lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string normalise(const std::string& s) {
    std::string out = to_lower_ascii(s);
    for (char& c : out)
        if (c == '/') c = '\\';
    return out;
}

// UTF-16LE bytes to UTF-8, lossy for anything outside the BMP.
std::string utf16le_to_utf8(std::span<const std::uint8_t> b, std::size_t offset, std::size_t max_bytes) {
    std::string out;
    for (std::size_t i = offset; i + 1 < b.size() && i - offset < max_bytes; i += 2) {
        const std::uint32_t cp = static_cast<std::uint32_t>(rd16(b, i));
        if (cp == 0) break;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

struct DirEntry {
    bool is_string = false;
    std::string name;  // when is_string
    std::uint32_t id = 0;
    bool is_subdir = false;
    std::uint32_t offset = 0;  // subdir offset, or data-entry offset
};

}  // namespace

std::optional<PeImage> PeImage::load(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return std::nullopt;
    }
    PeImage image;
    image.storage_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    image.bytes_ = image.storage_;

    const auto b = image.bytes_;
    if (b.size() < 0x40 || b[0] != 'M' || b[1] != 'Z') {
        error = "not an MZ image";
        return std::nullopt;
    }
    const std::uint32_t pe_off = rd32(b, 0x3C);
    if (pe_off + 0x18 > b.size() || std::memcmp(b.data() + pe_off, "PE\0\0", 4) != 0) {
        error = "no PE signature";
        return std::nullopt;
    }

    image.machine_ = rd16(b, pe_off + 4);
    const std::uint16_t num_sections = rd16(b, pe_off + 6);
    image.timestamp_ = rd32(b, pe_off + 8);
    const std::uint16_t opt_size = rd16(b, pe_off + 20);
    image.characteristics_ = rd16(b, pe_off + 22);

    const std::size_t opt = pe_off + 24;
    if (opt + opt_size > b.size()) {
        error = "optional header truncated";
        return std::nullopt;
    }
    const std::uint16_t magic = rd16(b, opt);
    const bool pe32_plus = magic == 0x20B;
    if (!pe32_plus && magic != 0x10B) {
        error = "unsupported optional header magic";
        return std::nullopt;
    }

    image.entry_point_rva_ = rd32(b, opt + 16);
    image.image_base_ = pe32_plus ? (static_cast<std::uint64_t>(rd32(b, opt + 24)) |
                                     (static_cast<std::uint64_t>(rd32(b, opt + 28)) << 32))
                                  : static_cast<std::uint64_t>(rd32(b, opt + 28));
    image.subsystem_ = rd16(b, opt + 68);

    const std::size_t num_dirs_off = opt + (pe32_plus ? 108 : 92);
    const std::size_t dirs_off = opt + (pe32_plus ? 112 : 96);
    const std::uint32_t num_dirs = rd32(b, num_dirs_off);
    if (num_dirs > kDebugDirectory) {
        image.resource_rva_ = rd32(b, dirs_off + kResourceDirectory * 8);
        image.resource_size_ = rd32(b, dirs_off + kResourceDirectory * 8 + 4);
        image.debug_rva_ = rd32(b, dirs_off + kDebugDirectory * 8);
        image.debug_size_ = rd32(b, dirs_off + kDebugDirectory * 8 + 4);
    }

    const std::size_t sec_off = opt + opt_size;
    for (std::uint16_t i = 0; i < num_sections; ++i) {
        const std::size_t o = sec_off + static_cast<std::size_t>(i) * 40;
        if (o + 40 > b.size()) {
            error = "section table truncated";
            return std::nullopt;
        }
        Section s;
        char name[9] = {};
        std::memcpy(name, b.data() + o, 8);
        s.name = name;
        s.virtual_size = rd32(b, o + 8);
        s.virtual_address = rd32(b, o + 12);
        s.raw_size = rd32(b, o + 16);
        s.raw_offset = rd32(b, o + 20);
        s.characteristics = rd32(b, o + 36);
        image.sections_.push_back(s);
    }

    image.parse_resources(error);
    return image;
}

bool PeImage::rva_to_offset(std::uint32_t rva, std::uint32_t& offset) const {
    for (const Section& s : sections_) {
        const std::uint32_t span = std::max(s.virtual_size, s.raw_size);
        if (rva >= s.virtual_address && rva < s.virtual_address + span) {
            const std::uint32_t delta = rva - s.virtual_address;
            if (delta >= s.raw_size) return false;  // in virtual-only tail
            offset = s.raw_offset + delta;
            return true;
        }
    }
    // Header-resident data (an .rsrc never lives here, but be permissive).
    if (rva < 0x1000) {
        offset = rva;
        return true;
    }
    return false;
}

bool PeImage::has_code_section() const {
    // IMAGE_SCN_CNT_CODE (0x20) or IMAGE_SCN_MEM_EXECUTE (0x20000000).
    for (const Section& s : sections_)
        if ((s.characteristics & 0x20u) != 0 || (s.characteristics & 0x20000000u) != 0) return true;
    return false;
}

void PeImage::parse_resources(std::string& error) {
    if (resource_rva_ == 0 || resource_size_ == 0) return;  // legitimately absent

    std::uint32_t base = 0;
    if (!rva_to_offset(resource_rva_, base)) {
        error = "resource directory RVA is not mapped";
        return;
    }
    const auto b = bytes_;
    if (base + 16 > b.size()) {
        error = "resource directory truncated";
        return;
    }

    const auto read_dir = [&](std::uint32_t off, std::vector<DirEntry>& out) -> bool {
        if (off + 16 > b.size()) return false;
        const std::uint16_t named = rd16(b, off + 12);
        const std::uint16_t ids = rd16(b, off + 14);
        const std::size_t total = static_cast<std::size_t>(named) + ids;
        if (off + 16 + total * 8 > b.size()) return false;
        out.reserve(total);
        for (std::size_t i = 0; i < total; ++i) {
            const std::size_t e = off + 16 + i * 8;
            DirEntry entry;
            const std::uint32_t name_field = rd32(b, e);
            if ((name_field & 0x80000000u) != 0) {
                entry.is_string = true;
                const std::uint32_t name_off = base + (name_field & 0x7FFFFFFFu);
                if (name_off + 2 > b.size()) return false;
                const std::uint16_t chars = rd16(b, name_off);
                entry.name = utf16le_to_utf8(b, name_off + 2,
                                             std::min<std::size_t>(static_cast<std::size_t>(chars) * 2,
                                                                   kMaxNameChars * 2));
            } else {
                entry.id = name_field;
            }
            const std::uint32_t offset_field = rd32(b, e + 4);
            entry.is_subdir = (offset_field & 0x80000000u) != 0;
            entry.offset = offset_field & 0x7FFFFFFFu;
            out.push_back(std::move(entry));
        }
        return true;
    };

    const auto label = [](const DirEntry& e) -> std::string {
        if (e.is_string) return e.name;
        return "#" + std::to_string(e.id);
    };

    std::vector<DirEntry> types;
    if (!read_dir(base, types)) {
        error = "failed to read resource type directory";
        return;
    }
    for (const DirEntry& type : types) {
        if (!type.is_subdir) continue;
        std::vector<DirEntry> names;
        if (!read_dir(base + type.offset, names)) continue;
        for (const DirEntry& name : names) {
            if (!name.is_subdir) continue;
            std::vector<DirEntry> langs;
            if (!read_dir(base + name.offset, langs)) continue;
            for (const DirEntry& lang : langs) {
                if (lang.is_subdir) continue;
                const std::uint32_t data_off = base + lang.offset;
                if (data_off + 16 > b.size()) continue;
                ResourceEntry rc;
                rc.type = label(type);
                rc.name = label(name);
                rc.name_is_string = name.is_string;
                rc.language = lang.is_string ? 0 : lang.id;
                rc.data_rva = rd32(b, data_off);
                rc.size = rd32(b, data_off + 4);
                rc.norm = normalise(rc.name);
                std::uint32_t off = 0;
                rc.file_offset = rva_to_offset(rc.data_rva, off) ? off : 0;
                resources_.push_back(std::move(rc));
            }
        }
    }
}

std::vector<std::uint8_t> PeImage::read(const ResourceEntry& entry) const {
    std::vector<std::uint8_t> out;
    if (entry.file_offset == 0 || entry.size == 0) return out;
    if (static_cast<std::size_t>(entry.file_offset) + entry.size > bytes_.size()) return out;
    out.assign(bytes_.begin() + entry.file_offset, bytes_.begin() + entry.file_offset + entry.size);
    return out;
}

const ResourceEntry* PeImage::find(const std::string& resource_name) const {
    const std::string want = normalise(resource_name);
    for (const ResourceEntry& r : resources_)
        if (r.norm == want) return &r;
    return nullptr;
}

std::vector<std::pair<std::string, std::size_t>> PeImage::type_counts() const {
    std::vector<std::pair<std::string, std::size_t>> out;
    for (const ResourceEntry& r : resources_) {
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == r.type; });
        if (it == out.end())
            out.emplace_back(r.type, 1);
        else
            ++it->second;
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& c) { return a.second > c.second; });
    return out;
}

std::string PeImage::codeview_pdb() const {
    if (debug_rva_ == 0 || debug_size_ == 0) return {};
    std::uint32_t off = 0;
    if (!rva_to_offset(debug_rva_, off)) return {};
    const auto b = bytes_;
    for (std::uint32_t i = 0; i + 28 <= debug_size_; i += 28) {
        const std::size_t e = off + i;
        if (e + 28 > b.size()) break;
        const std::uint32_t type = rd32(b, e + 12);
        if (type != 2) continue;  // IMAGE_DEBUG_TYPE_CODEVIEW
        const std::uint32_t size = rd32(b, e + 16);
        const std::uint32_t raw = rd32(b, e + 24);
        if (size < 24 || static_cast<std::size_t>(raw) + size > b.size()) return {};
        if (std::memcmp(b.data() + raw, "RSDS", 4) != 0) return {};
        const char* text = reinterpret_cast<const char*>(b.data() + raw + 24);
        const std::size_t capacity = size - 24;
        std::size_t length = 0;
        while (length < capacity && text[length] != '\0') ++length;
        return std::string(text, length);
    }
    return {};
}

}  // namespace pple
