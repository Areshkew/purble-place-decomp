#include "pple/mui.hpp"

#include <cstdlib>

#include "pple/pe_image.hpp"

namespace pple {
namespace {
Localiser g_localiser = nullptr;
void* g_localiser_user = nullptr;
}  // namespace

const std::u16string* MuiStrings::find(int id) const {
    const auto it = by_id.find(id);
    return it == by_id.end() ? nullptr : &it->second;
}

std::string utf8_from_utf16(const std::u16string& text) {
    std::string out;
    for (char16_t c : text) {
        const std::uint32_t cp = c;
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

std::string xml_payload_to_utf8(std::span<const std::uint8_t> d) {
    if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xFE) {
        std::u16string u;
        for (std::size_t i = 2; i + 1 < d.size(); i += 2)
            u.push_back(static_cast<char16_t>(d[i] | (d[i + 1] << 8)));
        return utf8_from_utf16(u);
    }
    std::size_t start = 0;
    if (d.size() >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) start = 3;
    return std::string(reinterpret_cast<const char*>(d.data() + start), d.size() - start);
}

bool mui_load(const PeImage& pe, MuiStrings& out, std::string& error) {
    out = MuiStrings{};
    for (const ResourceEntry& r : pe.resources()) {
        if (r.type != "#6") continue;
        if (r.name.empty() || r.name[0] != '#') continue;
        const int block = std::atoi(r.name.c_str() + 1);
        if (block <= 0) continue;
        const auto payload = pe.read(r);
        std::size_t at = 0;
        for (int slot = 0; slot < 16; ++slot) {
            if (at + 2 > payload.size()) break;
            const std::uint32_t len = static_cast<std::uint32_t>(payload[at]) | (payload[at + 1] << 8);
            at += 2;
            const std::size_t bytes = static_cast<std::size_t>(len) * 2;
            if (at + bytes > payload.size()) break;
            std::u16string text;
            text.reserve(len);
            for (std::uint32_t c = 0; c < len; ++c)
                text.push_back(static_cast<char16_t>(payload[at + c * 2] | (payload[at + c * 2 + 1] << 8)));
            at += bytes;
            if (len == 0) continue;
            out.by_id[(block - 1) * 16 + slot] = std::move(text);
            ++out.non_empty;
        }
        ++out.blocks;
    }
    if (out.blocks == 0) {
        error = "no RT_STRING resources";
        return false;
    }
    return true;
}

void set_localiser(Localiser localiser, void* user) {
    g_localiser = localiser;
    g_localiser_user = user;
}

Localiser localiser() { return g_localiser; }

}  // namespace pple
