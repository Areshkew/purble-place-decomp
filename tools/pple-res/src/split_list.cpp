#include "pple/split_list.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "pple/xml.hpp"

namespace pple {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

bool split_list_parse(std::span<const std::uint8_t> data, std::vector<SplitSheet>& out,
                      std::string& error) {
    out.clear();
    XmlNode root;
    if (!xml_parse(data, root, error)) return false;
    for (const XmlNode& child : root.children) {
        if (child.name != "Texture") continue;
        SplitSheet sheet;
        sheet.name = child.attr("name");
        if (sheet.name.empty()) continue;
        sheet.width = std::atoi(child.attr("width", "0").c_str());
        sheet.height = std::atoi(child.attr("height", "0").c_str());
        out.push_back(std::move(sheet));
    }
    if (out.empty()) {
        error = "no <Texture name= width= height=> entries under the root";
        return false;
    }
    return true;
}

bool split_list_load(const std::function<bool(const std::string&, std::vector<std::uint8_t>&)>& fetch,
                     std::vector<SplitSheet>& out, std::string& error) {
    out.clear();
    std::vector<std::uint8_t> payload;
    if (!fetch("SplitList.xml", payload)) {
        error = "SplitList.xml not found";
        return false;
    }
    return split_list_parse(payload, out, error);
}

const SplitSheet* split_sheet_find(const std::vector<SplitSheet>& list, const std::string& name) {
    for (const SplitSheet& s : list)
        if (s.name == name) return &s;
    const std::string want = lower(name);
    for (const SplitSheet& s : list)
        if (lower(s.name) == want) return &s;
    return nullptr;
}

std::string split_page_name(const std::string& sheet_name, unsigned page) {
    // The engine finds the last '.' and works backwards from the terminating NUL, so a name
    // with no '.' gets its last four characters shifted; SheetOffsetX-style names always
    // carry an extension, so this is a faithful-enough reproduction for real input.
    const std::size_t dot = sheet_name.rfind('.');
    if (dot == std::string::npos) return sheet_name;
    std::string stem = sheet_name.substr(0, dot) + "x2";
    stem.push_back(static_cast<char>('1' + (page % 10)));
    return stem + ".jpg";
}

}  // namespace pple
