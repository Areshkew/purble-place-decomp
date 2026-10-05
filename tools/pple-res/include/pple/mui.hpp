// .mui string tables.
//
// The labels in the XML carry values of the form "|id|context|English text//comment".  The
// engine resolves them through LocalizeMessage (PurblePlace.exe 0x1000B664C), which is the
// Win32 MUI lookup: NodeLabel::Load (0x1000A09F0) calls it for <FontName> and <FontSize> too,
// and NodeLabel::SetText (0x1000A1018) calls it whenever the text starts with '|'.
//
// A string-table block (RT_STRING, resource type 6) holds 16 consecutive length-prefixed
// UTF-16 strings and is *named* by its block number, so the string at slot i of block b has id
// (b - 1) * 16 + i.  The table is not localised per context: the "context" field in the XML
// (BAP_UI, ConCards, ...) exists only in the data, which is why the check in `pple-res
// muicheck` compares ids and text and not contexts.
#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace pple {

class PeImage;
struct ResourceEntry;

struct MuiStrings {
    std::map<int, std::u16string> by_id;
    std::size_t blocks = 0;
    std::size_t non_empty = 0;

    bool empty() const { return by_id.empty(); }
    const std::u16string* find(int id) const;
};

bool mui_load(const PeImage& pe, MuiStrings& out, std::string& error);

// UTF-16 (BMP) to UTF-8, for printing and comparing.
std::string utf8_from_utf16(const std::u16string& text);

// The XML resources are mostly ASCII, some UTF-8 with a BOM, some UTF-16LE with one.
std::string xml_payload_to_utf8(std::span<const std::uint8_t> data);

// A localiser resolves a "|id|context|English" reference.  `fallback` is the embedded English.
using Localiser = std::string (*)(int id, const std::string& fallback, void* user);

// Installs the localiser used by localized_default().  Pass nullptr to remove it.
void set_localiser(Localiser localiser, void* user);
Localiser localiser();

}  // namespace pple
