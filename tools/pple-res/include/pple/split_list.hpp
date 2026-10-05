// SplitList.xml — the per-sheet descriptor the engine reads at startup.
//
// ResourceManager::Initialize (PurblePlace.exe 0x1000A20B4) loads "SplitList.xml" through
// XmlManager, walks /Texture, and stores {name -> (width,height)} in a sorted list at
// g_pResourceManager+80 (0x1000A2178..0x1000A2315).  ResourceTextureD3D's constructor
// (0x1000A784C) looks its own name up in that list: a hit sets the "is split" flag at
// +152 and the virtual sheet size at +144/+148, and a miss logs
// "Tried to get split resource size on a non split resource %s" (0x1000A7921).
//
// The declared size is the *virtual* sheet.  At normal DPI the sheet still loads as one
// texture (ResourceTextureD3D::Recreate 0x1000A7FEC); when g_bDoubleDPI is set the virtual
// size is used directly and the content comes from up to four page resources named
// <stem>x2<page+1> (SetNameForDPI 0x1000A80DC inserts "x2", GetSplitTexture 0x1000A7E40
// inserts the page digit and refuses page >= 4), which is exactly the SHEET18X21..X24
// resource family.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace pple {

struct SplitSheet {
    std::string name;  // as written in SplitList.xml, e.g. "Sheets\purbleplacebuttonSheet18.sht"
    int width = 0;
    int height = 0;
};

// Parses a SplitList.xml payload.
bool split_list_parse(std::span<const std::uint8_t> data, std::vector<SplitSheet>& out,
                      std::string& error);

// Parses a SplitList.xml payload fetched by resource name (usually "SplitList.xml").
bool split_list_load(const std::function<bool(const std::string&, std::vector<std::uint8_t>&)>& fetch,
                     std::vector<SplitSheet>& out, std::string& error);

// Engine-matching lookup.  Exact match first, then a case-insensitive pass; the engine's
// comparison is not directly observable from the data because every shipped <Texture>
// value happens to match SplitList.xml's spelling exactly.
const SplitSheet* split_sheet_find(const std::vector<SplitSheet>& list, const std::string& name);

// "Sheets\x.sht" -> "Sheets\x21.jpg" (page 0..3).  This mirrors GetSplitTexture followed by
// the name rewrite inside CreateTextureFromSheet: "x2" before the extension, then the page
// digit, then the extension swapped for "jpg".
std::string split_page_name(const std::string& sheet_name, unsigned page);

}  // namespace pple
