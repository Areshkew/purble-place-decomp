#include "pple/xml.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace pple {
namespace {

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Resolves the five predefined entities plus numeric character references.  An
// unknown entity is copied through verbatim rather than dropped.
std::string unescape(const std::string& in) {
    if (in.find('&') == std::string::npos) return in;
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            continue;
        }
        const std::size_t semi = in.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out.push_back('&');
            continue;
        }
        const std::string name = in.substr(i + 1, semi - i - 1);
        if (name == "amp") out.push_back('&');
        else if (name == "lt") out.push_back('<');
        else if (name == "gt") out.push_back('>');
        else if (name == "quot") out.push_back('"');
        else if (name == "apos") out.push_back('\'');
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const std::string digits = hex ? name.substr(2) : name.substr(1);
            try {
                const std::uint32_t cp = static_cast<std::uint32_t>(std::stoul(digits, nullptr, hex ? 16 : 10));
                append_utf8(out, cp);
            } catch (...) {
                out += in.substr(i, semi - i + 1);
            }
        } else {
            out += in.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && is_space(s[b])) ++b;
    while (e > b && is_space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

struct Parser {
    const std::string& s;
    std::size_t p = 0;
    std::string error;

    explicit Parser(const std::string& text) : s(text) {}

    bool eof() const { return p >= s.size(); }
    char peek() const { return p < s.size() ? s[p] : '\0'; }
    bool starts_with(const char* lit) const { return s.compare(p, std::strlen(lit), lit) == 0; }
    void skip_space() { while (!eof() && is_space(s[p])) ++p; }

    void skip_misc() {
        for (;;) {
            skip_space();
            if (starts_with("<?")) {
                const std::size_t end = s.find("?>", p);
                if (end == std::string::npos) { p = s.size(); return; }
                p = end + 2;
            } else if (starts_with("<!--")) {
                const std::size_t end = s.find("-->", p);
                if (end == std::string::npos) { p = s.size(); return; }
                p = end + 3;
            } else if (starts_with("<!")) {  // DOCTYPE or similar: skip to '>'
                const std::size_t end = s.find('>', p);
                if (end == std::string::npos) { p = s.size(); return; }
                p = end + 1;
            } else {
                return;
            }
        }
    }

    std::string read_name() {
        const std::size_t begin = p;
        while (!eof()) {
            const char c = s[p];
            if (is_space(c) || c == '>' || c == '/' || c == '=') break;
            ++p;
        }
        return s.substr(begin, p - begin);
    }

    std::string read_quoted() {
        const char quote = peek();
        if (quote != '"' && quote != '\'') {
            error = "attribute value is not quoted";
            return {};
        }
        ++p;
        const std::size_t begin = p;
        while (!eof() && s[p] != quote) ++p;
        const std::string raw = s.substr(begin, p - begin);
        if (!eof()) ++p;
        return unescape(raw);
    }

    // Parses one element whose '<' has already been consumed.
    bool parse_element(XmlNode& node) {
        node.name = read_name();
        if (node.name.empty()) {
            error = "empty element name";
            return false;
        }
        for (;;) {
            skip_space();
            if (eof()) {
                error = "unterminated start tag <" + node.name + ">";
                return false;
            }
            if (starts_with("/>")) {
                p += 2;
                return true;
            }
            if (peek() == '>') {
                ++p;
                break;
            }
            XmlAttribute attribute;
            attribute.name = read_name();
            if (attribute.name.empty()) {
                error = "malformed attribute in <" + node.name + ">";
                return false;
            }
            skip_space();
            if (peek() == '=') {
                ++p;
                skip_space();
                attribute.value = read_quoted();
                if (!error.empty()) return false;
            }
            node.attributes.push_back(std::move(attribute));
        }

        // Content.
        std::string text;
        for (;;) {
            if (eof()) {
                error = "unterminated element <" + node.name + ">";
                return false;
            }
            if (starts_with("<!--")) {
                const std::size_t end = s.find("-->", p);
                p = end == std::string::npos ? s.size() : end + 3;
                continue;
            }
            if (starts_with("<![CDATA[")) {
                const std::size_t end = s.find("]]>", p);
                if (end == std::string::npos) { error = "unterminated CDATA"; return false; }
                text += s.substr(p + 9, end - (p + 9));
                p = end + 3;
                continue;
            }
            if (starts_with("</")) {
                p += 2;
                const std::string closing = read_name();
                skip_space();
                if (peek() != '>') {
                    error = "malformed end tag </" + closing + ">";
                    return false;
                }
                ++p;
                if (closing != node.name) {
                    error = "mismatched tags: <" + node.name + "> closed by </" + closing + ">";
                    return false;
                }
                node.text = trim(unescape(text));
                return true;
            }
            if (peek() == '<') {
                ++p;
                XmlNode child;
                if (!parse_element(child)) return false;
                node.children.push_back(std::move(child));
                continue;
            }
            text.push_back(s[p]);
            ++p;
        }
    }
};

}  // namespace

std::string xml_to_utf8(std::span<const std::uint8_t> data) {
    if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        std::string out;
        for (std::size_t i = 2; i + 1 < data.size(); i += 2)
            append_utf8(out, static_cast<std::uint32_t>(data[i] | (data[i + 1] << 8)));
        return out;
    }
    if (data.size() >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        std::string out;
        for (std::size_t i = 2; i + 1 < data.size(); i += 2)
            append_utf8(out, static_cast<std::uint32_t>(data[i + 1] | (data[i] << 8)));
        return out;
    }
    std::size_t begin = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) begin = 3;
    return std::string(reinterpret_cast<const char*>(data.data() + begin), data.size() - begin);
}

bool xml_parse(std::span<const std::uint8_t> data, XmlNode& root, std::string& error) {
    const std::string text = xml_to_utf8(data);
    Parser parser(text);
    parser.skip_misc();
    if (parser.eof() || parser.peek() != '<') {
        error = "no root element";
        return false;
    }
    ++parser.p;
    if (!parser.parse_element(root)) {
        error = parser.error;
        return false;
    }
    return true;
}

const XmlNode* XmlNode::child(const std::string& child_name) const {
    for (const XmlNode& c : children)
        if (c.name == child_name) return &c;
    return nullptr;
}

const XmlNode* XmlNode::find(const std::string& slash_path) const {
    const XmlNode* current = this;
    std::size_t begin = 0;
    while (begin <= slash_path.size()) {
        const std::size_t slash = slash_path.find('/', begin);
        const std::string part = slash_path.substr(begin, slash == std::string::npos ? std::string::npos
                                                                                    : slash - begin);
        if (!part.empty()) {
            current = current->child(part);
            if (current == nullptr) return nullptr;
        }
        if (slash == std::string::npos) break;
        begin = slash + 1;
    }
    return current;
}

std::string XmlNode::attr(const std::string& attribute_name, const std::string& fallback) const {
    for (const XmlAttribute& a : attributes)
        if (a.name == attribute_name) return a.value;
    return fallback;
}

std::string XmlNode::child_text(const std::string& child_name) const {
    const XmlNode* c = child(child_name);
    return c == nullptr ? std::string{} : c->text;
}

void XmlNode::collect_paths(std::vector<std::string>& out, const std::string& prefix) const {
    const std::string here = prefix.empty() ? name : prefix + "/" + name;
    out.push_back(here);
    for (const XmlNode& c : children) c.collect_paths(out, here);
}

}  // namespace pple
