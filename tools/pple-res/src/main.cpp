// pple-res — resource loader for the Purble Place asset DLLs.
//
//   pple-res summary <pe>...
//   pple-res list    <pe> [--grep TEXT] [--limit N]
//   pple-res verify  <pe>...
//   pple-res extract <pe> <resource-name> <outfile> [--container]
//   pple-res find    <pe>... <text> [--limit N]
//   pple-res split   <pe>...
//   pple-res cards   <pe>...
//   pple-res rgmh    <file>
//   pple-res sha256  <file>
//
// No dependencies beyond the C++ standard library.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pple/cab.hpp"
#include "pple/concentration.hpp"
#include "pple/mui.hpp"
#include "pple/pe_image.hpp"
#include "pple/png.hpp"
#include "pple/rgmh.hpp"
#include "pple/sha256.hpp"
#include "pple/split_list.hpp"
#include "pple/wma.hpp"
#include "pple/xml.hpp"

namespace {

using pple::CabArchive;
using pple::PeImage;
using pple::ResourceEntry;

std::uint16_t rd16(std::span<const std::uint8_t> b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}
std::uint32_t rd32(std::span<const std::uint8_t> b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) | (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

bool starts_with(std::span<const std::uint8_t> b, std::string_view s) {
    return b.size() >= s.size() && std::memcmp(b.data(), s.data(), s.size()) == 0;
}

std::string to_hex(std::span<const std::uint8_t> b, std::size_t n) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < std::min(n, b.size()); ++i) {
        out.push_back(kDigits[b[i] >> 4]);
        out.push_back(kDigits[b[i] & 0xF]);
    }
    return out;
}

std::vector<std::uint8_t> slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Decoded names for the integer resource types that actually appear in these files.
std::string type_name(const std::string& type) {
    if (type.empty() || type[0] != '#') return type;
    switch (std::stoul(type.substr(1))) {
        case 3: return "#3 (RT_ICON)";
        case 4: return "#4 (RT_MENU)";
        case 5: return "#5 (RT_DIALOG)";
        case 6: return "#6 (RT_STRING)";
        case 9: return "#9 (RT_ACCELERATOR)";
        case 14: return "#14 (RT_GROUP_ICON)";
        case 16: return "#16 (RT_VERSION)";
        case 24: return "#24 (RT_MANIFEST)";
        default: return type;
    }
}

bool spill(const std::string& path, std::span<const std::uint8_t> data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return out.good();
}

// UTF-16 (BMP only) to UTF-8, for printing the container's wide names.
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

std::string human(std::size_t n) {
    char buf[32];
    if (n >= 1024 * 1024)
        std::snprintf(buf, sizeof buf, "%.2f MB", static_cast<double>(n) / (1024 * 1024));
    else if (n >= 1024)
        std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(n) / 1024);
    else
        std::snprintf(buf, sizeof buf, "%zu B", n);
    return buf;
}

// Names the payload from its leading bytes, so extraction results are self-checking.
std::string identify(std::span<const std::uint8_t> d) {
    if (CabArchive::looks_like_cab(d)) return "CAB (MSCF)";
    if (starts_with(d, "\x89PNG\r\n\x1a\n") && d.size() >= 24) {
        const std::uint32_t w = (static_cast<std::uint32_t>(d[16]) << 24) | (d[17] << 16) | (d[18] << 8) | d[19];
        const std::uint32_t h = (static_cast<std::uint32_t>(d[20]) << 24) | (d[21] << 16) | (d[22] << 8) | d[23];
        char buf[64];
        std::snprintf(buf, sizeof buf, "PNG %ux%u", w, h);
        return buf;
    }
    if (starts_with(d, "BM") && d.size() >= 30) {
        const std::uint32_t size = rd32(d, 2);
        const std::int32_t w = static_cast<std::int32_t>(rd32(d, 18));
        const std::int32_t h = static_cast<std::int32_t>(rd32(d, 22));
        const std::uint16_t bpp = rd16(d, 28);
        char buf[96];
        std::snprintf(buf, sizeof buf, "BMP %dx%d %ubpp size_field=%u", w, h, bpp, size);
        return buf;
    }
    if (starts_with(d, "\xff\xd8\xff")) return "JPEG";
    if (starts_with(d, "\x30\x26\xb2\x75")) return "ASF/WMA";
    if (starts_with(d, "RIFF")) return "RIFF";
    if (starts_with(d, "<?xml") || starts_with(d, "\xef\xbb\xbf<")) return "XML (utf-8)";
    if (d.size() >= 4 && (d[0] == '<' || (d[0] == ' ' && d[1] == '<'))) return "XML (ascii)";
    if (d.size() >= 2 && d[1] == 0 && d[0] >= 0x20) return "UTF-16LE text";
    return "raw " + to_hex(d, 8);
}

struct CabStats {
    std::size_t cab_resources = 0;
    std::size_t resources = 0;
    std::size_t inflated_ok = 0;
    std::size_t inflated_failed = 0;
    std::uint64_t stored_bytes = 0;
    std::uint64_t inflated_bytes = 0;
    std::uint64_t declared_bytes = 0;
    std::size_t block_mismatches = 0;
    std::uint32_t min_stored = 0, max_stored = 0;
    std::uint32_t min_file = 0, max_file = 0;
    std::vector<std::string> first_failures;
};

void print_header(const PeImage& pe, const std::string& path) {
    std::printf("%s\n", path.c_str());
    std::printf("  machine          : 0x%04x%s\n", pe.machine(),
                pe.machine() == 0x8664 ? " (x64)" : (pe.machine() == 0x14C ? " (x86)" : ""));
    std::printf("  type             : %s, subsystem %u\n", pe.is_dll() ? "DLL" : "EXE", pe.subsystem());
    std::printf("  image base       : 0x%llx\n", static_cast<unsigned long long>(pe.image_base()));
    std::printf("  entry point RVA  : 0x%x%s\n", pe.entry_point_rva(),
                pe.entry_point_rva() == 0 ? "  (loader does not call DllMain)" : "");
    std::printf("  code section     : %s\n", pe.has_code_section() ? "yes" : "none");
    std::printf("  file size        : %s (%zu bytes)\n", human(pe.file_bytes().size()).c_str(),
                pe.file_bytes().size());
    std::printf("  sections         :\n");
    for (const auto& s : pe.sections()) {
        std::printf("    %-8s vsize=0x%-8x raw=0x%-8x chars=0x%08x%s\n", s.name.c_str(), s.virtual_size,
                    s.raw_size, s.characteristics, (s.characteristics & 0x20000000u) ? "  X" : "");
    }
    const std::string pdb = pe.codeview_pdb();
    if (!pdb.empty()) std::printf("  CodeView PDB     : %s\n", pdb.c_str());
}

int cmd_summary(const std::vector<std::string>& files) {
    for (const std::string& path : files) {
        std::string error;
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        print_header(*pe, path);
        std::printf("  resources        : %zu\n", pe->resources().size());
        for (const auto& [type, count] : pe->type_counts())
            std::printf("    type %-20s %6zu\n", type_name(type).c_str(), count);

        std::map<std::string, std::pair<std::size_t, std::uint64_t>> by_kind;
        std::size_t cabs = 0;
        for (const auto& r : pe->resources()) {
            const auto payload = pe->read(r);
            const std::string kind = identify(payload);
            // Collapse dimensions so the histogram is readable.
            std::string key = kind;
            if (key.rfind("PNG ", 0) == 0 || key.rfind("BMP ", 0) == 0) key = key.substr(0, key.find(' '));
            if (key.rfind("raw ", 0) == 0) key = "unknown";
            auto& slot = by_kind[key];
            ++slot.first;
            slot.second += payload.size();
            if (key == "CAB") ++cabs;
        }
        std::printf("  payload kinds    :\n");
        for (const auto& [kind, stat] : by_kind)
            std::printf("    %-16s %6zu   %s\n", kind.c_str(), stat.first, human(stat.second).c_str());
        if (cabs > 0) std::printf("  CAB archives     : %zu\n", cabs);
        std::printf("\n");
    }
    return 0;
}

int cmd_list(const std::string& path, const std::string& grep, std::size_t limit) {
    std::string error;
    const auto pe = PeImage::load(path, error);
    if (!pe) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return 1;
    }
    std::printf("%-8s %-8s %-10s %10s  %s\n", "type", "lang", "size", "kind", "name");
    std::size_t shown = 0;
    for (const auto& r : pe->resources()) {
        if (!grep.empty()) {
            std::string hay = r.name;
            std::string needle = grep;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            if (hay.find(needle) == std::string::npos) continue;
        }
        const auto payload = pe->read(r);
        std::printf("%-8s 0x%04x   %10u  %-14s %s\n", r.type.c_str(), r.language, r.size,
                    identify(payload).c_str(), r.name.c_str());
        if (++shown >= limit) break;
    }
    std::printf("-- %zu shown of %zu resources\n", shown, pe->resources().size());
    return 0;
}

// Case-insensitive ASCII substring search plus a UTF-16LE variant, over every resource
// payload in every named module.  Answers "which XML mentions this sheet / this id?"
// without extracting 840 resources one at a time.
int cmd_find(const std::vector<std::string>& files, const std::string& needle, std::size_t limit) {
    std::string lower = needle;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    // UTF-16LE form: needle bytes interleaved with NUL, lowercased per 16-bit unit.
    std::vector<std::uint8_t> utf16;
    for (unsigned char c : lower) {
        utf16.push_back(static_cast<std::uint8_t>(::tolower(c)));
        utf16.push_back(0);
    }

    const auto context = [](std::span<const std::uint8_t> d, std::size_t off, std::size_t width) {
        std::string out;
        for (std::size_t i = off; i < d.size() && out.size() < 72; i += width) {
            const unsigned char c = d[i];
            out.push_back(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '.');
        }
        return out;
    };

    int hits = 0;
    for (const std::string& path : files) {
        std::string error;
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        for (const auto& r : pe->resources()) {
            if (hits >= static_cast<int>(limit)) break;
            const auto payload = pe->read(r);
            for (std::size_t i = 0; i + lower.size() <= payload.size(); ++i) {
                std::size_t k = 0;
                while (k < lower.size() &&
                       ::tolower(static_cast<unsigned char>(payload[i + k])) ==
                           static_cast<unsigned char>(lower[k]))
                    ++k;
                if (k != lower.size()) continue;
                std::printf("%-14s %-52s off %-9zu %s\n", r.type.c_str(), r.name.c_str(), i,
                            context(payload, i, 1).c_str());
                ++hits;
                if (hits >= static_cast<int>(limit)) break;
            }
            if (hits >= static_cast<int>(limit)) break;
            for (std::size_t i = 0; i + utf16.size() <= payload.size(); ++i) {
                if (std::memcmp(payload.data() + i, utf16.data(), utf16.size()) != 0) continue;
                std::printf("%-14s %-52s off %-9zu %s   [utf-16le]\n", r.type.c_str(), r.name.c_str(), i,
                            context(payload, i, 2).c_str());
                ++hits;
                if (hits >= static_cast<int>(limit)) break;
            }
            if (hits >= static_cast<int>(limit)) break;
        }
    }
    std::printf("-- %d hit(s) for \"%s\"\n", hits, needle.c_str());
    return hits == 0 ? 2 : 0;
}

// SplitList.xml describes the sheets whose content is too large for one texture.  The
// engine reads it at startup (ResourceManager::Initialize 0x1000A20B4); a sheet listed here
// is "split" and, when the engine is in double-DPI mode, is drawn from up to four page
// resources named <stem>x2<page+1> instead of the plain <stem>.jpg/<stem>.bmp pair.
int cmd_split(const std::vector<std::string>& files) {
    for (const std::string& path : files) {
        std::string error;
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        const auto* slot = pe->find("SplitList.xml");
        if (slot == nullptr) {
            std::printf("%s\n  no SplitList.xml\n\n", path.c_str());
            continue;
        }
        std::vector<pple::SplitSheet> sheets;
        std::string parse_error;
        if (!pple::split_list_parse(pe->read(*slot), sheets, parse_error)) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), parse_error.c_str());
            return 1;
        }
        std::printf("%s\n  SplitList.xml: %zu split sheet(s)\n", path.c_str(), sheets.size());
        for (const pple::SplitSheet& s : sheets) {
            std::printf("    %-48s virtual %dx%d\n", s.name.c_str(), s.width, s.height);
            for (unsigned page = 0; page < 4; ++page) {
                const std::string name = pple::split_page_name(s.name, page);
                const auto* page_slot = pe->find(name);
                if (page_slot == nullptr) continue;
                std::printf("       page %u  %-48s %8u bytes  %s\n", page + 1, name.c_str(),
                            page_slot->size, identify(pe->read(*page_slot)).c_str());
            }
        }
        std::printf("\n");
    }
    return 0;
}

// The card deck definition.  Board::BuildCardLists (0x100088FE0) walks this file and fills
// pools by <type>; Board::GetCardList (0x100089AAC) draws from those pools according to the
// level's <CardPairs> string.  A card is not a sheet frame: <path> is a sprite node definition.
int cmd_cards(const std::vector<std::string>& files) {
    for (const std::string& path : files) {
        std::string error;
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        const auto* slot = pe->find("UI\\CONCENTRATION\\CARDS.XML");
        if (slot == nullptr) {
            std::printf("%s\n  no UI\\CONCENTRATION\\CARDS.XML\n\n", path.c_str());
            continue;
        }
        pple::XmlNode root;
        std::string parse_error;
        if (!pple::xml_parse(pe->read(*slot), root, parse_error)) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), parse_error.c_str());
            return 1;
        }
        std::printf("%s\n  %s, root <%s Type=\"%s\">\n", path.c_str(), slot->name.c_str(),
                    root.name.c_str(), root.attr("Type").c_str());
        std::printf("  %-4s %-4s %-6s %-28s %s\n", "id", "type", "chef", "description", "sprite definition");
        int count = 0;
        for (const pple::XmlNode& card : root.children) {
            if (card.name != "Card") continue;
            ++count;
            const std::string id = card.child_text("id");
            const std::string type = card.child_text("type");
            std::string chef = card.child_text("chefitem");
            if (!chef.empty() && chef[0] == '\\') chef = chef.substr(1);
            std::string desc = card.child_text("desc");
            const std::size_t bar = desc.rfind('|');
            if (bar != std::string::npos) desc = desc.substr(bar + 1);
            std::printf("  %-4s %-4s %-6s %-28s %s\n", id.c_str(), type.c_str(), chef.c_str(), desc.c_str(),
                        card.child_text("path").c_str());
        }
        std::printf("  %d card(s)\n", count);
        std::printf("  pool      Board offset  chosen by\n");
        std::printf("  normal    +192          a plain <CardPairs> letter, from the shuffled type-0 pool\n");
        std::printf("  type 4    +216          plain letter while <NumTrickier> remains\n");
        std::printf("  type 5    +240          plain letter while <NumTricky> remains\n");
        std::printf("  type 3    +372  'M'     Mixer\n");
        std::printf("  type 6    +376  'T'     Time Bonus\n");
        std::printf("  type 8    +384  'J'     Wild Card\n");
        std::printf("  type 1    +396  'P'     Purble Chef\n");
        std::printf("  type 7    +380  -       none in this file\n");
        std::printf("                  'Z'     random between +396 and +384\n\n");
    }
    return 0;
}

// Purble Pairs: build a real board from the shipped data and, on request, play it out.  The
// autoplay is the rules test - it solves the board through the same click/tick API the viewer
// would use, so a win proves the deck, the match rule and the scoring agree.
int cmd_pairs(const std::string& path, const std::string& difficulty, int level_index, unsigned seed,
              bool autoplay, bool show_layout) {
    std::string error;
    const auto pe = PeImage::load(path, error);
    if (!pe) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return 1;
    }
    const auto* scene_slot = pe->find("UI\\CONCENTRATION\\CONCENTRATIONGAME.XML");
    const auto* cards_slot = pe->find("UI\\CONCENTRATION\\CARDS.XML");
    if (scene_slot == nullptr || cards_slot == nullptr) {
        std::fprintf(stderr, "%s: needs UI\\CONCENTRATION\\CONCENTRATIONGAME.XML and CARDS.XML\n",
                     path.c_str());
        return 1;
    }

    pple::CardPools pools;
    std::string pools_error;
    if (!pple::concentration_pools_parse(pe->read(*cards_slot), pools, pools_error)) {
        std::fprintf(stderr, "CARDS.XML: %s\n", pools_error.c_str());
        return 1;
    }
    pple::ConcentrationConstants constants;
    std::vector<pple::ConcentrationLevel> levels;
    std::string game_error;
    if (!pple::concentration_game_parse(pe->read(*scene_slot), constants, levels, game_error)) {
        std::fprintf(stderr, "CONCENTRATIONGAME.XML: %s\n", game_error.c_str());
        return 1;
    }
    pple::CardsBox box;
    std::string box_error;
    pple::concentration_cards_box_parse(pe->read(*scene_slot), box, box_error);

    std::printf("%s\n", path.c_str());
    std::printf("  card pools   : %zu cards; normal %zu, tricky(type 4) %zu, super-tricky(type 5) %zu\n",
                pools.cards.size(), pools.normal.size(), pools.tricky.size(), pools.super_tricky.size());
    std::printf("                 chef(id %d) mixer(id %d) time-bonus(id %d) wild(id %d)\n", pools.chef,
                pools.mixer, pools.time_bonus, pools.wild);
    std::printf("  constants    : RevealDuration %d, TimeBonusPercent %d, MatchPoints normal/tricky/super %d/%d/%d\n",
                constants.reveal_duration, constants.time_bonus_percent, constants.match_points_normal,
                constants.match_points_tricky, constants.match_points_super_tricky);

    const pple::ConcentrationLevel* chosen = nullptr;
    for (const pple::ConcentrationLevel& l : levels) {
        if (!difficulty.empty() && l.difficulty != difficulty) continue;
        if (l.level_index != level_index) continue;
        chosen = &l;
        break;
    }
    if (chosen == nullptr) {
        std::fprintf(stderr, "no level %d for difficulty \"%s\"\n", level_index,
                     difficulty.empty() ? "(first)" : difficulty.c_str());
        return 1;
    }
    std::printf("  level        : %s #%d  %dx%d, spacing %dx%d, %s, NumCardTypes %d, tricky %d trickier %d\n",
                chosen->difficulty.c_str(), chosen->level_index, chosen->num_cols, chosen->num_rows,
                chosen->x_spacing, chosen->y_spacing,
                chosen->timed() ? (std::to_string(chosen->total_time_seconds) + " s (" +
                                   std::to_string(chosen->total_ticks()) + " ticks)").c_str()
                                : "untimed (no <TotalTime>)",
                chosen->num_card_types, chosen->num_tricky, chosen->num_trickier);
    std::printf("  CardPairs    : \"%s\" (%zu characters for %d cells = %d pairs)\n",
                chosen->card_pairs.c_str(), chosen->card_pairs.size(), chosen->cell_count(),
                chosen->cell_count() / 2);
    std::printf("  <Cards> box  : %dx%d (the scale reference)\n", box.width, box.height);

    pple::ConcentrationBoard board;
    // The board needs the difficulty's whole <Levels> list, because Board::NewLevel advances
    // through it: Intermediate plays two boards and Advanced four.
    std::vector<pple::ConcentrationLevel> chosen_levels;
    std::size_t chosen_at = 0;
    for (const pple::ConcentrationLevel& l : levels) {
        if (l.difficulty != chosen->difficulty) continue;
        if (l.level_index == chosen->level_index) chosen_at = chosen_levels.size();
        chosen_levels.push_back(l);
    }
    std::printf("  levels       : %zu in \"%s\" (Board::NewLevel walks them)\n", chosen_levels.size(),
                chosen->difficulty.c_str());
    if (!board.reset(chosen_levels, chosen_at, pools, constants, box, seed)) {
        std::fprintf(stderr, "board reset failed\n");
        return 1;
    }
    std::printf("  layout       : %d cells, cell %dx%d, scale %d%%, first cell at %d,%d\n",
                board.cell_count(), board.slots().front().w, board.slots().front().h,
                board.slots().front().scale_percent, board.slots().front().x, board.slots().front().y);
    {
        int distinct_normal = 0;
        std::vector<int> seen;
        for (int id : board.deck()) {
            if (id == pools.wild) continue;
            bool have = false;
            for (int s : seen) have = have || s == id;
            if (!have) {
                seen.push_back(id);
                ++distinct_normal;
            }
        }
        const int wilds =
            static_cast<int>(std::count(board.deck().begin(), board.deck().end(), pools.wild));
        std::printf("  deck         : %d cards; %d distinct normal ids (NumCardTypes says %d), %d wild\n",
                    static_cast<int>(board.deck().size()), distinct_normal, chosen->num_card_types, wilds);
    }

    if (show_layout) {
        std::printf("\n  index  col row     x     y   w   h   id  type  face\n");
        for (const pple::ConcentrationSlot& s : board.slots()) {
            const pple::CardDef* def = pools.find(s.card_id);
            std::printf("  %5d  %3d %3d  %4d  %4d %3d %3d %4d  %4d  %s\n", s.index, s.col, s.row, s.x, s.y,
                        s.w, s.h, s.card_id, def != nullptr ? def->type : -1,
                        def != nullptr ? def->path.c_str() : "?");
        }
    }

    if (!autoplay) return 0;

    int clicks = 0, mismatches = 0;
    std::vector<int> order(static_cast<std::size_t>(board.cell_count()));
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::mt19937 rng(seed ^ 0x5eedu);
    std::shuffle(order.begin(), order.end(), rng);

    int guard = 0;
    while (!board.finished() && guard++ < 200000) {
        if (board.lockout_ticks() > 0) {
            board.tick();
            continue;
        }
        int first = -1, second = -1;
        for (int i : order) {
            if (board.face(i) != pple::CardFace::Hidden) continue;
            if (first < 0) {
                first = i;
                continue;
            }
            if (board.card_id(i) == board.card_id(first)) {
                second = i;
                break;
            }
        }
        // Nothing pairs with `first`: on an odd board that is the lone Wild Card, which can be
        // matched against any one other card (Card::DoMatcher).
        if (first >= 0 && second < 0) {
            for (int i : order) {
                if (i == first || board.face(i) != pple::CardFace::Hidden) continue;
                second = i;
                break;
            }
        }
        if (first < 0 || second < 0) break;
        const int level_before = board.level_position();
        const pple::ClickResult r1 = board.click(first);
        // A level change re-deals the board, so the second half of the pair no longer means
        // anything: stop here and let the next pass read the new layout.
        if (board.level_position() != level_before) {
            ++clicks;
            continue;
        }
        const pple::ClickResult r2 = board.click(second);
        ++clicks;
        // Card::DoMatcher's mismatch branch is the only one that arms the lockout; a Wild Card
        // match succeeds without a MatchMade (`++matches_` is not run for it), so count the
        // mismatches from the result rather than from the face.
        if (r2.mismatch) ++mismatches;
        if (r1.relaid_out || r2.relaid_out) {
            // The Mixer reshuffled the remaining cards: the pair list is stale.
            order.assign(static_cast<std::size_t>(board.cell_count()), 0);
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
            std::shuffle(order.begin(), order.end(), rng);
        }
    }

    std::printf("\n  autoplay     : %d pairs clicked, %d not matched, score %d, matches %d/%d\n", clicks,
                mismatches, board.score(), board.matches(), board.round_pairs_total());
    if (chosen->timed())
        std::printf("                 time left %.1f s of %d s, %s\n",
                    static_cast<double>(board.seconds_remaining()), chosen->total_time_seconds,
                    board.won() ? "WON" : (board.lost() ? "LOST" : "unfinished"));
    else
        std::printf("                 untimed, %s\n", board.won() ? "WON" : (board.lost() ? "LOST" : "unfinished"));
    return board.won() ? 0 : 2;
}

// AUDIO\AUDIO.XML: <Audio><Music>…</Music><Sounds><EVENT><File>…</File><Volume>n</Volume>
// <Buffers>n</Buffers></EVENT>…</Sounds></Audio>.  A <File> is a name under AUDIO\SFX\, which is
// the resource-resolution rule DllFileMgr::LoadResource applies to audio-style names.  The
// event element's own name is the key the game code plays by.
int cmd_audio(const std::vector<std::string>& files, bool list_events) {
    std::size_t total = 0, resolved = 0, missing = 0;
    for (const std::string& path : files) {
        std::string error;
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        const auto* slot = pe->find("AUDIO\\AUDIO.XML");
        if (slot == nullptr) {
            std::printf("%s\n  no AUDIO\\AUDIO.XML\n\n", path.c_str());
            continue;
        }
        pple::XmlNode root;
        std::string parse_error;
        if (!pple::xml_parse(pe->read(*slot), root, parse_error)) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), parse_error.c_str());
            return 1;
        }
        std::size_t events = 0, here_resolved = 0, here_missing = 0;
        std::vector<std::string> unresolved;
        const auto walk = [&](const pple::XmlNode& group) {
            for (const pple::XmlNode& event : group.children) {
                const pple::XmlNode* file = event.child("File");
                if (file == nullptr) continue;
                ++events;
                const std::string name = file->text;
                if (name.empty()) continue;
                std::string resource = "AUDIO\\SFX\\" + name;
                for (char& c : resource)
                    if (c == '/') c = '\\';
                const auto* found = pe->find(resource);
                if (found != nullptr) {
                    ++here_resolved;
                    if (list_events)
                        std::printf("  %-28s vol %-3s buf %-2s -> %s (%u bytes)\n", event.name.c_str(),
                                    event.child_text("Volume").c_str(),
                                    event.child_text("Buffers").c_str(), found->name.c_str(), found->size);
                } else {
                    ++here_missing;
                    if (unresolved.size() < 6) unresolved.push_back(event.name + " -> " + resource);
                }
            }
        };
        for (const pple::XmlNode& group : root.children) {
            if (group.name == "Music" || group.name == "Sounds") {
                walk(group);
            } else if (group.child("File") != nullptr) {
                // tolerate events directly under <Audio>
                const pple::XmlNode wrapper{"", {}, "", {group}};
                walk(wrapper);
            }
        }
        std::printf("%s\n  AUDIO.XML: %zu events, %zu resolve to a resource, %zu do not\n", path.c_str(),
                    events, here_resolved, here_missing);
        for (const std::string& u : unresolved) std::printf("    unresolved: %s\n", u.c_str());
        total += events;
        resolved += here_resolved;
        missing += here_missing;
    }
    if (files.size() > 1)
        std::printf("total: %zu events, %zu resolved, %zu unresolved\n", total, resolved, missing);
    return 0;  // a missing sound is tolerated: the manifest header says so
}
// ---------------------------------------------------------------------------------------
// ASF/WMA container inspection.
//
// Every audio resource begins with 30 26 B2 75 8E 66 CF 11 A6 D9 00 AA 00 62 CE 6C, the ASF
// Header Object.  This walks the header objects and reads the two that matter for playback:
// File Properties (duration, packet count) and the audio Stream Properties (the WAVEFORMATEX
// that names the codec).  It is the groundwork for decoding, which has to happen through
// Media Foundation: SDL3_mixer ships decoders for aiff, au, flac, fluidsynth, gme, mp3, opus,
// raw, sinewave, timidity, vorbis, voc, wav, wavpack and xmp only, and has no WMA decoder at
// all (verified against src/decoder_*.c in libsdl-org/SDL_mixer).
struct AsfInfo {
    std::string codec;
    unsigned channels = 0;
    unsigned sample_rate = 0;
    unsigned bits = 0;
    unsigned avg_bytes_per_sec = 0;
    unsigned long long play_ms = 0;
    unsigned long long packets = 0;
    unsigned max_bitrate = 0;
    bool audio_stream = false;
    bool ok = false;
};

const char* wma_codec_name(unsigned tag) {
    switch (tag) {
        case 0x0160: return "WMA v1";
        case 0x0161: return "WMA v2 (standard)";
        case 0x0162: return "WMA Pro";
        case 0x0163: return "WMA Lossless";
        case 0x000A: return "WMA Voice";
        case 0x0001: return "PCM";
        default: return "unknown";
    }
}

bool asf_parse(std::span<const std::uint8_t> d, AsfInfo& out) {
    out = AsfInfo{};
    static const unsigned char kHeader[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                              0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
    // 8CABDCA1-A947-11CF-8EE6-00C00C205365, little-endian as stored
    static const unsigned char kFileProps[16] = {0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11,
                                                 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
    static const unsigned char kStreamProps[16] = {0x91, 0x07, 0xDC, 0xB7, 0xB7, 0xA9, 0xCF, 0x11,
                                                   0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
    static const unsigned char kAudioMedia[16] = {0x40, 0x9E, 0x69, 0xF8, 0x4D, 0x5B, 0xCF, 0x11,
                                                  0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B};
    if (d.size() < 30 || std::memcmp(d.data(), kHeader, 16) != 0) return false;
    const auto u32 = [&](std::size_t o) {
        return static_cast<unsigned>(d[o]) | (static_cast<unsigned>(d[o + 1]) << 8) |
               (static_cast<unsigned>(d[o + 2]) << 16) | (static_cast<unsigned>(d[o + 3]) << 24);
    };
    const auto u64 = [&](std::size_t o) {
        return static_cast<unsigned long long>(u32(o)) |
               (static_cast<unsigned long long>(u32(o + 4)) << 32);
    };
    const unsigned long long header_size = u64(16);
    if (header_size > d.size()) return false;
    std::size_t at = 30;
    while (at + 24 <= header_size) {
        const auto* guid = d.data() + at;
        const unsigned long long size = u64(at + 16);
        if (size < 24 || at + size > header_size) break;
        // Only the first 12 GUID bytes are compared: the published File Properties GUID ends
        // 8EE6-00C00C205365 but these files carry 8EE4 in that position (Stream Properties does
        // use 8EE6).  The first 12 bytes are unique among the objects present.
        if (std::memcmp(guid, kFileProps, 12) == 0 && size >= 80) {
            out.packets = u64(at + 24 + 32);          // Data Packets Count
            const unsigned long long play = u64(at + 24 + 40);
            const unsigned long long preroll = u64(at + 24 + 56);
            out.play_ms = play / 10000ULL > preroll ? play / 10000ULL - preroll : 0;
            const unsigned flags = u32(at + 24 + 64);
            // bits 0 and 1 set means the broadcast flag, i.e. unknown size and duration
            out.max_bitrate = (flags & 1) != 0 ? 0 : u32(at + 24 + 76);
        } else if (std::memcmp(guid, kStreamProps, 12) == 0 && size >= 78) {
            const std::size_t body = at + 24;
            if (std::memcmp(d.data() + body, kAudioMedia, 12) == 0) {
                const unsigned type_len = u32(body + 40);
                const std::size_t wf = body + 54;
                if (type_len >= 16 && wf + 16 <= at + size) {
                    const unsigned tag = d[wf] | (d[wf + 1] << 8);
                    out.codec = wma_codec_name(tag);
                    out.channels = d[wf + 2] | (d[wf + 3] << 8);
                    out.sample_rate = u32(wf + 4);
                    out.avg_bytes_per_sec = u32(wf + 8);
                    out.bits = d[wf + 14] | (d[wf + 15] << 8);
                    out.audio_stream = true;
                }
            }
        }
        at += static_cast<std::size_t>(size);
    }
    out.ok = out.audio_stream;
    return out.audio_stream;
}

// Parses every ASF resource in the modules and reports the codec parameters.
int cmd_asf(const std::vector<std::string>& files, bool list_all) {
    std::size_t found = 0, ok = 0;
    std::map<std::string, std::size_t> codecs;
    for (const std::string& path : files) {
        std::string error;
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        std::printf("%s\n", path.c_str());
        std::size_t here = 0;
        for (const ResourceEntry& r : pe->resources()) {
            const auto payload = pe->read(r);
            if (payload.size() < 16 || payload[0] != 0x30 || payload[1] != 0x26 || payload[2] != 0xB2 ||
                payload[3] != 0x75)
                continue;
            ++here;
            AsfInfo info;
            if (asf_parse(payload, info)) {
                ++ok;
                codecs[info.codec]++;
                if (list_all)
                    std::printf("  %-52s %-20s %uch %5u Hz %2u bit %6u B/s %6llu ms %5llu pkts\n",
                                r.name.c_str(), info.codec.c_str(), info.channels, info.sample_rate,
                                info.bits, info.avg_bytes_per_sec, info.play_ms, info.packets);
            }
        }
        std::printf("  ASF/WMA resources: %zu\n", here);
        found += here;
    }
    std::printf("total ASF/WMA resources: %zu, parsed with an audio stream: %zu\n", found, ok);
    for (const auto& [codec, count] : codecs) std::printf("  codec %-22s %zu\n", codec.c_str(), count);
    return 0;
}
// Decodes WMA through Media Foundation.  `--all` is the verification: every ASF resource is
// decoded and the decoded length is compared with the duration the container declares.
int cmd_wma(const std::vector<std::string>& files, const std::string& one, const std::string& out_path,
            bool all) {
    std::string start_error;
    if (!pple::mf_start(start_error)) {
        std::fprintf(stderr, "media foundation: %s\n", start_error.c_str());
        return 1;
    }
    std::size_t decoded = 0, failed = 0, duration_agrees = 0;
    double total_seconds = 0.0;
    std::vector<std::string> failures;
    for (const std::string& path : files) {
        std::string error;
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            pple::mf_stop();
            return 1;
        }
        for (const ResourceEntry& r : pe->resources()) {
            const auto payload = pe->read(r);
            const bool is_asf = payload.size() >= 4 && payload[0] == 0x30 && payload[1] == 0x26 &&
                                payload[2] == 0xB2 && payload[3] == 0x75;
            if (!is_asf) continue;
            if (!all && (one.empty() || r.name != one)) continue;
            pple::PcmAudio audio;
            std::string decode_error;
            if (!pple::wma_decode(payload, audio, decode_error)) {
                ++failed;
                if (failures.size() < 8) failures.push_back(r.name + ": " + decode_error);
                continue;
            }
            ++decoded;
            total_seconds += audio.seconds();
            // Cross-check: the ASF File Properties object declares the duration independently of
            // anything the decoder does, so the two should agree.
            AsfInfo info;
            std::string duration_note = "  (no declared duration)";
            if (asf_parse(payload, info) && info.play_ms > 0) {
                const double declared = static_cast<double>(info.play_ms) / 1000.0;
                const double delta = audio.seconds() - declared;
                if (delta < -0.05 || delta > 0.25) {
                    ++failed;
                    if (failures.size() < 8)
                        failures.push_back(r.name + ": decoded " + std::to_string(audio.seconds()) +
                                           " s but the container declares " + std::to_string(declared) + " s");
                } else {
                    ++duration_agrees;
                }
                char buf[64];
                std::snprintf(buf, sizeof buf, "  declared %.3f s (%+.3f)", declared, delta);
                duration_note = buf;
            }
            if (!out_path.empty() && !one.empty()) {
                const bool ok = pple::wav_write(out_path, audio);
                std::printf("  wrote %s (%s)\n", out_path.c_str(), ok ? "ok" : "FAILED");
            }
            if (all)
                std::printf("  %-52s %uch %5u Hz %6.3f s  %7zu frames  %8zu bytes PCM%s\n",
                            r.name.c_str(), audio.channels, audio.sample_rate, audio.seconds(),
                            audio.frames(), audio.samples.size() * 2, duration_note.c_str());
        }
    }
    std::printf("decoded %zu WMA resource(s), %zu failed, %.2f s of audio in total\n", decoded, failed,
                total_seconds);
    std::printf("  decoded length agrees with the ASF declared duration for %zu of %zu\n", duration_agrees,
                decoded);
    for (const std::string& f : failures) std::printf("    FAIL %s\n", f.c_str());
    pple::mf_stop();
    return failed == 0 && decoded > 0 ? 0 : 2;
}
// pple-res strings / muicheck, on top of pple::mui.
int cmd_strings(const std::string& path, int want_id, const std::string& grep, std::size_t limit) {
    std::string error;
    const auto pe = PeImage::load(path, error);
    if (!pe) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return 1;
    }
    pple::MuiStrings strings;
    if (!pple::mui_load(*pe, strings, error)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return 1;
    }
    std::printf("%s\n  %zu RT_STRING blocks, %zu non-empty strings, ids %d..%d\n", path.c_str(),
                strings.blocks, strings.non_empty,
                strings.by_id.empty() ? 0 : strings.by_id.begin()->first,
                strings.by_id.empty() ? 0 : strings.by_id.rbegin()->first);
    std::size_t shown = 0, matched = 0;
    for (const auto& [id, text] : strings.by_id) {
        if (want_id >= 0 && id != want_id) continue;
        const std::string utf8 = pple::utf8_from_utf16(text);
        if (!grep.empty()) {
            std::string hay = utf8, needle = grep;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            if (hay.find(needle) == std::string::npos) continue;
        }
        ++matched;
        if (shown < limit) {
            std::printf("  id %-6d %s\n", id, utf8.c_str());
            ++shown;
        }
    }
    std::printf("  %zu match(es), %zu shown\n", matched, shown);
    return matched == 0 ? 2 : 0;
}

// Checks every "|id|context|English//comment" reference in the XML resources against the .mui.
// The XML escapes a newline as the two characters \n; the .mui holds a real one, so both sides
// are unescaped before comparing.
int cmd_muicheck(const std::string& mui_path, const std::vector<std::string>& resources) {
    std::string error;
    const auto mui = PeImage::load(mui_path, error);
    if (!mui) {
        std::fprintf(stderr, "%s: %s\n", mui_path.c_str(), error.c_str());
        return 1;
    }
    pple::MuiStrings strings;
    if (!pple::mui_load(*mui, strings, error)) {
        std::fprintf(stderr, "%s: %s\n", mui_path.c_str(), error.c_str());
        return 1;
    }
    std::printf("%s: %zu strings, ids %d..%d\n", mui_path.c_str(), strings.non_empty,
                strings.by_id.empty() ? 0 : strings.by_id.begin()->first,
                strings.by_id.empty() ? 0 : strings.by_id.rbegin()->first);

    const auto unescape = [](std::string s) {
        for (std::size_t i = 0; i + 1 < s.size();) {
            if (s[i] == '\\' && (s[i + 1] == 'n' || s[i + 1] == 'r' || s[i + 1] == 't')) {
                s[i] = s[i + 1] == 'n' ? '\n' : (s[i + 1] == 'r' ? '\r' : '\t');
                s.erase(i + 1, 1);
            }
            ++i;
        }
        return s;
    };
    std::size_t seen = 0, same = 0, differ = 0, missing = 0;
    std::vector<std::string> differ_examples, missing_examples;
    for (const std::string& path : resources) {
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        for (const ResourceEntry& r : pe->resources()) {
            const auto payload = pe->read(r);
            if (payload.size() < 8) continue;
            const bool looks_like_xml = payload[0] == '<' || (payload[0] == 0xEF && payload[1] == 0xBB) ||
                                        (payload[0] == 0xFF && payload[1] == 0xFE);
            if (!looks_like_xml) continue;
            const std::string text = pple::xml_payload_to_utf8(payload);
            for (std::size_t i = 0; i + 3 < text.size(); ++i) {
                if (text[i] != '|' || !std::isdigit(static_cast<unsigned char>(text[i + 1]))) continue;
                std::size_t j = i + 1;
                int id = 0;
                while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) {
                    id = id * 10 + (text[j] - '0');
                    ++j;
                }
                if (j >= text.size() || text[j] != '|') continue;
                const std::size_t context_end = text.find('|', j + 1);
                if (context_end == std::string::npos) continue;
                std::size_t stop = text.find("//", context_end + 1);
                if (stop == std::string::npos) stop = text.size();
                for (char terminator : {'<', '"', '>', '|'}) {
                    const std::size_t at = text.find(terminator, context_end + 1);
                    if (at != std::string::npos && at < stop) stop = at;
                }
                std::string english = unescape(text.substr(context_end + 1, stop - (context_end + 1)));
                while (!english.empty() && (english.back() == ' ' || english.back() == '\r' ||
                                            english.back() == '\n' || english.back() == '\t'))
                    english.pop_back();
                i = context_end;
                ++seen;
                const std::u16string* found = strings.find(id);
                if (found == nullptr) {
                    ++missing;
                    if (missing_examples.size() < 6)
                        missing_examples.push_back("id " + std::to_string(id) + " \"" + english + "\"");
                    continue;
                }
                std::string mui_text = unescape(pple::utf8_from_utf16(*found));
                while (!mui_text.empty() && (mui_text.back() == ' ' || mui_text.back() == '\r' ||
                                             mui_text.back() == '\n' || mui_text.back() == '\t'))
                    mui_text.pop_back();
                if (mui_text == english) {
                    ++same;
                } else {
                    ++differ;
                    if (differ_examples.size() < 6)
                        differ_examples.push_back("id " + std::to_string(id) + " xml \"" + english +
                                                  "\" vs mui \"" + mui_text + "\"");
                }
            }
        }
    }
    std::printf("  localisation ids referenced: %zu\n", seen);
    std::printf("  identical to the .mui       : %zu (%.1f%%)\n", same,
                seen == 0 ? 0.0 : 100.0 * static_cast<double>(same) / static_cast<double>(seen));
    std::printf("  different text              : %zu\n", differ);
    std::printf("  id absent from this .mui    : %zu\n", missing);
    for (const std::string& e : differ_examples) std::printf("    differs: %s\n", e.c_str());
    for (const std::string& e : missing_examples) std::printf("    absent : %s\n", e.c_str());
    return 0;
}
int cmd_verify(const std::vector<std::string>& files) {
    int failures = 0;    for (const std::string& path : files) {
        std::string error;
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        CabStats st;
        for (const auto& r : pe->resources()) {
            const auto payload = pe->read(r);
            ++st.resources;
            if (!CabArchive::looks_like_cab(payload)) continue;
            ++st.cab_resources;
            st.stored_bytes += payload.size();
            st.min_stored = st.min_stored ? std::min(st.min_stored, r.size) : r.size;
            st.max_stored = std::max(st.max_stored, r.size);

            std::string cab_error;
            const auto cab = CabArchive::parse(payload, cab_error);
            if (!cab) {
                ++st.inflated_failed;
                if (st.first_failures.size() < 5) st.first_failures.push_back(r.name + ": " + cab_error);
                continue;
            }
            std::vector<std::uint8_t> out;
            std::size_t bad_blocks = 0;
            std::string err;
            const bool ok = cab->extract_folder(payload, 0, out, err, &bad_blocks);
            st.block_mismatches += bad_blocks;
            st.inflated_bytes += out.size();
            st.declared_bytes += cab->files.empty() ? 0 : cab->files[0].uncompressed_size;
            st.min_file = st.min_file ? std::min(st.min_file, cab->files[0].uncompressed_size)
                                      : cab->files[0].uncompressed_size;
            st.max_file = std::max(st.max_file, cab->files[0].uncompressed_size);
            if (ok && out.size() == cab->files[0].uncompressed_size) {
                ++st.inflated_ok;
            } else {
                ++st.inflated_failed;
                if (st.first_failures.size() < 5)
                    st.first_failures.push_back(r.name + ": " + (err.empty() ? "cbFile mismatch" : err));
            }
        }
        std::printf("%s\n", path.c_str());
        std::printf("  resources                : %zu\n", st.resources);
        std::printf("  CAB resources            : %zu\n", st.cab_resources);
        if (st.cab_resources) {
            std::printf("  stored bytes             : %llu (%s)  min %u max %u\n",
                        static_cast<unsigned long long>(st.stored_bytes), human(st.stored_bytes).c_str(),
                        st.min_stored, st.max_stored);
            std::printf("  inflated bytes           : %llu (%s)\n",
                        static_cast<unsigned long long>(st.inflated_bytes), human(st.inflated_bytes).c_str());
            std::printf("  declared cbFile total    : %llu (%s)  min %u max %u\n",
                        static_cast<unsigned long long>(st.declared_bytes), human(st.declared_bytes).c_str(),
                        st.min_file, st.max_file);
            std::printf("  ratio stored/declared    : %.1f%%\n",
                        st.declared_bytes ? 100.0 * static_cast<double>(st.stored_bytes) /
                                                static_cast<double>(st.declared_bytes)
                                          : 0.0);
            std::printf("  inflated and size-checked: %zu ok, %zu failed\n", st.inflated_ok, st.inflated_failed);
            std::printf("  block size mismatches    : %zu\n", st.block_mismatches);
            for (const std::string& f : st.first_failures) std::printf("    FAIL %s\n", f.c_str());
        }
        if (st.inflated_failed != 0 || st.block_mismatches != 0) ++failures;
        std::printf("\n");
    }
    return failures == 0 ? 0 : 2;
}

int cmd_extract(const std::string& path, const std::string& name, const std::string& out_path,
                bool keep_container) {
    std::string error;
    const auto pe = PeImage::load(path, error);
    if (!pe) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return 1;
    }
    const auto* entry = pe->find(name);
    if (entry == nullptr) {
        std::fprintf(stderr, "resource not found: %s\n", name.c_str());
        return 1;
    }
    const auto payload = pe->read(*entry);
    std::printf("resource   : %s\n", entry->name.c_str());
    std::printf("  type/lang: %s / 0x%04x\n", entry->type.c_str(), entry->language);
    std::printf("  file off : 0x%x, %zu bytes (%s)\n", entry->file_offset, payload.size(),
                human(payload.size()).c_str());
    std::printf("  head     : %s\n", to_hex(payload, 16).c_str());
    std::printf("  identify : %s\n", identify(payload).c_str());

    std::span<const std::uint8_t> write_span = payload;
    std::vector<std::uint8_t> inflated;
    if (!keep_container && CabArchive::looks_like_cab(payload)) {
        std::string cab_error;
        const auto cab = CabArchive::parse(payload, cab_error);
        if (!cab) {
            std::fprintf(stderr, "CAB parse failed: %s\n", cab_error.c_str());
            return 1;
        }
        std::printf("  cabinet  : version %s, %zu folder(s), %zu file(s), compression %s\n",
                    cab->version_string().c_str(), cab->folders.size(), cab->files.size(),
                    cab->folders.empty() ? "?" : cab->folders[0].compression_name().c_str());
        std::printf("  inner    : %s, declared %u bytes\n", cab->files[0].name.c_str(),
                    cab->files[0].uncompressed_size);
        std::string err;
        if (!cab->extract_single(payload, inflated, err)) {
            std::fprintf(stderr, "CAB extract failed: %s\n", err.c_str());
            return 1;
        }
        std::printf("  blocks   : %zu CFDATA, inflated %zu bytes (matches declared: %s)\n",
                    cab->blocks.size(), inflated.size(),
                    inflated.size() == cab->files[0].uncompressed_size ? "yes" : "NO");
        std::printf("  identify : %s\n", identify(inflated).c_str());
        write_span = inflated;
    }

    if (!spill(out_path, write_span)) {
        std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
        return 1;
    }
    std::printf("  wrote    : %s (%zu bytes)\n", out_path.c_str(), write_span.size());
    std::printf("  sha256   : %s\n", pple::sha256_hex(write_span).c_str());
    return 0;
}

// Conservative report on the "RGMH" save-file container.  Field names are NOT
// asserted: the two functions that write and read this header have not been
// reversed yet, so every field is printed by offset and the checks below only state
// what can be verified arithmetically from the file itself.
int cmd_rgmh(const std::string& path) {
    const std::vector<std::uint8_t> data = slurp(path);
    if (data.empty()) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        return 1;
    }
    const std::span<const std::uint8_t> d(data);
    if (!starts_with(d, "RGMH")) {
        std::fprintf(stderr, "%s does not start with RGMH\n", path.c_str());
        return 1;
    }
    std::printf("%s (%zu bytes)\n", path.c_str(), data.size());
    std::printf("  magic            : %.4s\n", reinterpret_cast<const char*>(d.data()));
    for (std::size_t off : {std::size_t{4}, std::size_t{8}, std::size_t{0x0C}, std::size_t{0x10},
                            std::size_t{0x14}})
        std::printf("  u32 @0x%02zx       : %u (0x%x)\n", off, rd32(d, off), rd32(d, off));

    const std::uint8_t* g = d.data() + 0x18;
    std::printf("  guid @0x18       : %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
                g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13],
                g[14], g[15]);

    std::string name;
    for (std::size_t i = 0x28; i + 1 < d.size(); i += 2) {
        if (rd16(d, i) == 0) break;
        const std::uint32_t cp = rd16(d, i);
        name.push_back(cp < 0x80 ? static_cast<char>(cp) : '?');
    }
    std::printf("  utf16 @0x28      : \"%s\"\n", name.c_str());

    const auto find = [&](std::string_view needle, std::size_t from) -> std::size_t {
        for (std::size_t i = from; i + needle.size() <= d.size(); ++i)
            if (std::memcmp(d.data() + i, needle.data(), needle.size()) == 0) return i;
        return 0;
    };

    const std::size_t png = find("\x89PNG\r\n\x1a\n", 0x28);
    std::printf("  PNG signature    : %s\n", png ? ("offset " + std::to_string(png)).c_str() : "not found");
    if (png != 0) {
        const std::size_t iend = find("IEND\xae\x42\x60\x82", png);
        if (iend != 0) {
            const std::size_t png_end = iend + 8;
            std::printf("  PNG IEND         : offset %zu, so the image occupies [%zu, %zu) = %zu bytes\n",
                        iend, png, png_end, png_end - png);
            std::printf("                     u32@0x08 (%u) == PNG offset -> %s\n", rd32(d, 0x08),
                        rd32(d, 0x08) == png ? "true" : "false");
            const std::size_t len_off = png_end;
            if (len_off + 6 <= d.size()) {
                const std::uint32_t len_field = rd32(d, len_off);
                const bool bom = d[len_off + 4] == 0xFF && d[len_off + 5] == 0xFE;
                const std::size_t xml_begin = len_off + (bom ? 6 : 4);
                const std::size_t xml_bytes = d.size() - xml_begin;
                std::printf("  after PNG        : u32 = %u, next 2 bytes %s BOM, XML begins at %zu\n", len_field,
                            bom ? "are FF FE" : "are not a", xml_begin);
                std::printf("                     XML payload to EOF = %zu bytes\n", xml_bytes);
                std::printf("  checks           : u32 == payload bytes      -> %s\n",
                            len_field == xml_bytes ? "true" : "false");
                std::printf("                     u32 == payload bytes + 2  -> %s\n",
                            len_field == xml_bytes + 2 ? "true" : "false");
                std::printf("                     u32 == payload bytes / 2  -> %s\n",
                            len_field == xml_bytes / 2 ? "true" : "false");
                std::printf("                     u32 == 2*units + 2        -> %s\n",
                            len_field == (xml_bytes / 2) * 2 + 2 ? "true" : "false");
                std::printf("                     u32@0x14 (%u) == payload  -> %s\n", rd32(d, 0x14),
                            rd32(d, 0x14) == xml_bytes ? "true" : "false");
                std::printf("  XML starts with  : %s\n", identify(d.subspan(xml_begin)).c_str());
            }
        }
    }
    std::printf("  layout note      : header runs to %zu; bytes 0x40..%zu carry fields this report does not name\n",
                png, png);
    return 0;
}

// Reads an RGMH file through pple::rgmh_read and re-emits it through pple::rgmh_write, then
// compares the two byte for byte.  A match is the proof that the writer fills every byte the
// engine's writer fills and nothing else.
int cmd_rgmh_write(const std::string& in_path, const std::string& out_path) {
    const std::vector<std::uint8_t> original = slurp(in_path);
    if (original.empty()) {
        std::fprintf(stderr, "cannot read %s\n", in_path.c_str());
        return 1;
    }
    pple::RgmhFile file;
    std::string error;
    if (!pple::rgmh_read(original, file, error)) {
        std::fprintf(stderr, "rgmh read failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", in_path.c_str());
    std::printf("  header           : %u bytes, version %u\n", file.header_size, file.version);
    std::printf("  u32 @0x0C/@0x10  : %u / %u\n", file.field_0x0C, file.field_0x10);
    std::printf("  guid             : %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
                file.guid[3], file.guid[2], file.guid[1], file.guid[0], file.guid[5], file.guid[4],
                file.guid[7], file.guid[6], file.guid[8], file.guid[9], file.guid[10], file.guid[11],
                file.guid[12], file.guid[13], file.guid[14], file.guid[15]);
    std::printf("  project name     : \"%s\" (%zu units at 0x28)\n", utf8_from_utf16(file.project_name).c_str(),
                file.project_name.size());
    std::printf("  file name        : \"%s\" (%zu units at 0x828)\n", utf8_from_utf16(file.file_name).c_str(),
                file.file_name.size());
    std::printf("  thumbnail        : %zu bytes at %zu\n", file.thumbnail.size(), file.thumbnail_offset());
    std::printf("  u32 length       : %u, then %zu bytes of BOM + XML (2*%zu + 2)\n", file.xml_length,
                file.xml.size(), file.xml.size() / 2 - 1);

    const std::vector<std::uint8_t> rebuilt = pple::rgmh_write(file);
    if (!out_path.empty() && !spill(out_path, rebuilt)) {
        std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
        return 1;
    }
    std::printf("  original         : %zu bytes  sha256 %s\n", original.size(),
                pple::sha256_hex(original).c_str());
    std::printf("  re-emitted       : %zu bytes  sha256 %s\n", rebuilt.size(),
                pple::sha256_hex(rebuilt).c_str());
    const std::size_t diff = pple::first_difference(original, rebuilt);
    if (diff == std::string::npos) {
        std::printf("  round trip       : BYTE-IDENTICAL\n");
        return 0;
    }
    std::printf("  round trip       : DIFFERS at offset %zu (0x%zx)\n", diff, diff);
    const std::size_t from = diff > 16 ? diff - 16 : 0;
    for (std::size_t i = from; i < diff + 16 && i < original.size() && i < rebuilt.size(); ++i)
        std::printf("    %6zu  %02x -> %02x%s\n", i, original[i], rebuilt[i], i == diff ? "   <- first" : "");
    return 2;
}

void usage() {
    std::fprintf(stderr,
                 "pple-res — Purble Place resource loader\n\n"
                 "  pple-res summary <pe>...\n"
                 "  pple-res list    <pe> [--grep TEXT] [--limit N]\n"
                 "  pple-res verify  <pe>...\n"
                 "  pple-res extract <pe> <resource-name> <outfile> [--container]\n"
                 "  pple-res find    <pe>... <text> [--limit N]\n"
                 "  pple-res split   <pe>...\n"
                 "  pple-res wma     <pe>... [--all] [--resource NAME --out FILE.wav]   decode through Media Foundation\n"
                 "  pple-res asf     <pe>... [--list]   every ASF/WMA resource and its codec parameters\n"
                 "  pple-res audio   <pe>... [--list]   AUDIO.XML events and their resources\n"
                 "  pple-res strings <file.mui> [--id N] [--grep TEXT] [--limit N]\n"
                 "  pple-res muicheck <file.mui> <pe>...   validate the |id|context|text ids\n"
                 "  pple-res cards   <pe>...    the Concentration card deck and its pools\n"
                 "  pple-res pairs   <pe> [--difficulty D] [--level N] [--seed N] [--autoplay] [--layout]\n"
                 "  pple-res png     <pe> <resource-name> [outfile.ppm]\n"
                 "  pple-res pngscan <pe>...\n"
                 "  pple-res rgmh    <file>\n"
                 "  pple-res rgmhrt  <in> [out]     read then re-emit, compare byte for byte\n"
                 "  pple-res sha256  <file>\n");
}

// Decodes every PNG resource in a module, so the decoder is exercised against real
// game assets rather than a test vector.
int cmd_pngscan(const std::vector<std::string>& files) {
    int failures = 0;
    for (const std::string& path : files) {
        std::string error;
        const auto pe = PeImage::load(path, error);
        if (!pe) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        std::size_t pngs = 0, ok = 0, interlaced = 0;
        std::vector<std::string> problems;
        std::map<std::string, std::size_t> formats;
        for (const auto& r : pe->resources()) {
            const auto payload = pe->read(r);
            std::uint32_t w = 0, h = 0;
            int depth = 0, colour = 0;
            bool il = false;
            if (!pple::png_probe(payload, w, h, depth, colour, il)) continue;
            ++pngs;
            if (il) ++interlaced;
            pple::PngImage image;
            std::string png_error;
            if (png_decode(payload, image, png_error)) {
                ++ok;
                formats["depth " + std::to_string(image.bit_depth) + " " + image.colour_type_name() +
                        (image.has_alpha ? " +alpha" : "")]++;
            } else if (problems.size() < 8) {
                problems.push_back(r.name + ": " + png_error);
            }
        }
        std::printf("%s\n  PNG resources: %zu, decoded: %zu, interlaced: %zu\n", path.c_str(), pngs, ok,
                    interlaced);
        for (const auto& [format, count] : formats) std::printf("    %-28s %4zu\n", format.c_str(), count);
        for (const std::string& p : problems) std::printf("    FAIL %s\n", p.c_str());
        if (ok != pngs) ++failures;
        std::printf("\n");
    }
    return failures == 0 ? 0 : 2;
}

int cmd_png(const std::string& path, const std::string& name, const std::string& out_path) {
    std::string error;
    const auto pe = PeImage::load(path, error);
    if (!pe) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return 1;
    }
    const auto* entry = pe->find(name);
    if (entry == nullptr) {
        std::fprintf(stderr, "resource not found: %s\n", name.c_str());
        return 1;
    }
    const auto payload = pe->read(*entry);
    pple::PngImage image;
    std::string png_error;
    if (!png_decode(payload, image, png_error)) {
        std::fprintf(stderr, "decode failed: %s\n", png_error.c_str());
        return 1;
    }
    std::printf("resource : %s\n", entry->name.c_str());
    std::printf("  decoded: %ux%u, bit depth %d, %s%s, %zu bytes RGBA\n", image.width, image.height,
                image.bit_depth, image.colour_type_name().c_str(), image.has_alpha ? " +alpha" : "",
                image.rgba.size());
    std::printf("  RGBA at (0,0): %02x %02x %02x %02x\n", image.rgba[0], image.rgba[1], image.rgba[2],
                image.rgba[3]);
    if (out_path.empty()) return 0;

    // Binary PPM (P6) of the RGB channels, so the decode can be eyeballed.
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
        return 1;
    }
    out << "P6\n" << image.width << " " << image.height << "\n255\n";
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(image.width) * image.height * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(image.width) * image.height; ++i) {
        rgb[i * 3 + 0] = image.rgba[i * 4 + 0];
        rgb[i * 3 + 1] = image.rgba[i * 4 + 1];
        rgb[i * 3 + 2] = image.rgba[i * 4 + 2];
    }
    out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    std::printf("  wrote  : %s (%zu bytes PPM)\n", out_path.c_str(), rgb.size() + 32);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        usage();
        return args.empty() ? 1 : 0;
    }
    const std::string cmd = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    auto flag_value = [&](const std::string& flag, const std::string& fallback) {
        for (std::size_t i = 0; i + 1 < rest.size(); ++i)
            if (rest[i] == flag) return rest[i + 1];
        return fallback;
    };
    const auto has_flag = [&](const std::string& flag) {
        return std::find(rest.begin(), rest.end(), flag) != rest.end();
    };
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == "--grep" || rest[i] == "--limit" || rest[i] == "--id" ||
            rest[i] == "--resource" || rest[i] == "--out" ||
            rest[i] == "--difficulty" ||
            rest[i] == "--level" || rest[i] == "--seed") {
            ++i;  // skip this flag's value
            continue;
        }
        if (!rest[i].empty() && rest[i][0] == '-') continue;  // valueless flag
        positional.push_back(rest[i]);
    }

    if (cmd == "summary") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_summary(positional);
    }
    if (cmd == "list") {
        if (positional.empty()) { usage(); return 1; }
        const std::size_t limit = static_cast<std::size_t>(std::stoul(flag_value("--limit", "1000000")));
        return cmd_list(positional[0], flag_value("--grep", ""), limit);
    }
    if (cmd == "verify") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_verify(positional);
    }
    if (cmd == "extract") {
        if (positional.size() < 3) { usage(); return 1; }
        return cmd_extract(positional[0], positional[1], positional[2], has_flag("--container"));
    }
    if (cmd == "find") {
        if (positional.size() < 2) { usage(); return 1; }
        const std::string text = positional.back();
        positional.pop_back();
        const std::size_t limit = static_cast<std::size_t>(std::stoul(flag_value("--limit", "1000")));
        return cmd_find(positional, text, limit);
    }
    if (cmd == "pairs") {
        if (positional.empty()) { usage(); return 1; }
        const std::string difficulty = flag_value("--difficulty", "");
        const int level_index = std::atoi(flag_value("--level", "0").c_str());
        const unsigned seed = static_cast<unsigned>(std::stoul(flag_value("--seed", "1")));
        return cmd_pairs(positional[0], difficulty, level_index, seed, has_flag("--autoplay"),
                         has_flag("--layout"));
    }
    if (cmd == "cards") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_cards(positional);
    }
    if (cmd == "audio") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_audio(positional, has_flag("--list"));
    }
    if (cmd == "wma") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_wma(positional, flag_value("--resource", ""), flag_value("--out", ""),
                       has_flag("--all"));
    }
    if (cmd == "asf") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_asf(positional, has_flag("--list"));
    }
    if (cmd == "strings") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_strings(positional[0], std::atoi(flag_value("--id", "-1").c_str()),
                           flag_value("--grep", ""),
                           static_cast<std::size_t>(std::stoul(flag_value("--limit", "40"))));
    }
    if (cmd == "muicheck") {
        if (positional.size() < 2) { usage(); return 1; }
        const std::string mui_path = positional[0];
        positional.erase(positional.begin());
        return cmd_muicheck(mui_path, positional);
    }
    if (cmd == "split") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_split(positional);
    }
    if (cmd == "pngscan") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_pngscan(positional);
    }
    if (cmd == "png") {
        if (positional.size() < 2) { usage(); return 1; }
        return cmd_png(positional[0], positional[1], positional.size() > 2 ? positional[2] : "");
    }
    if (cmd == "rgmh") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_rgmh(positional[0]);
    }
    if (cmd == "rgmhrt") {
        if (positional.empty()) { usage(); return 1; }
        return cmd_rgmh_write(positional[0], positional.size() > 1 ? positional[1] : "");
    }
    if (cmd == "sha256") {
        if (positional.empty()) { usage(); return 1; }
        const auto data = slurp(positional[0]);
        if (data.empty()) { std::fprintf(stderr, "cannot read %s\n", positional[0].c_str()); return 1; }
        std::printf("%s  %s\n", pple::sha256_hex(data).c_str(), positional[0].c_str());
        return 0;
    }
    usage();
    return 1;
}
