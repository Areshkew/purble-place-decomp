// Minimal XML reader for the game's data files.
//
// The shipped XML is simple: a declaration, comments, nested elements, attributes in
// double or single quotes, and text.  No namespaces, no CDATA, no DTDs.  Input may be
// UTF-8 with or without a BOM, or UTF-16LE/BE with a BOM, because the resource set
// contains both.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pple {

struct XmlAttribute {
    std::string name;
    std::string value;
};

struct XmlNode {
    std::string name;
    std::vector<XmlAttribute> attributes;
    std::string text;  // character data directly inside this element
    std::vector<XmlNode> children;

    const XmlNode* child(const std::string& child_name) const;
    const XmlNode* find(const std::string& slash_path) const;  // "A/B/C"
    std::string attr(const std::string& attribute_name, const std::string& fallback = {}) const;
    // Text of a direct child element, or empty.
    std::string child_text(const std::string& child_name) const;
    // Recursively collects `path` values joined with '/', one entry per element.
    void collect_paths(std::vector<std::string>& out, const std::string& prefix = {}) const;
};

// Decodes the byte stream (BOM aware) and parses it.  Returns false with a message.
bool xml_parse(std::span<const std::uint8_t> data, XmlNode& root, std::string& error);

// UTF-16/UTF-8 BOM aware conversion to UTF-8 text.
std::string xml_to_utf8(std::span<const std::uint8_t> data);

}  // namespace pple
