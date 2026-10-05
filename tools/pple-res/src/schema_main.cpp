// pple-xml — inventory of the game's XML data model, read as data rather than guessed
// from the code.
//
//   pple-xml <pe>... [--refs] [--examples N] [--path FILTER] [--list]
//
// Produces:
//   * parse coverage per module and the root elements actually used
//   * the element path histogram with the attributes each path carries and example values
//   * resource-reference resolution: every element whose text looks like a resource path is
//     resolved against all loaded modules using the loader's own rules
//     (exact, then ".cab" appended; ".sht" is tried as ".jpg" first, as the texture loader
//     rewrites the extension, and as ".bmp" + ".cab" for the no-DXT variant)
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "pple/pe_image.hpp"
#include "pple/sha256.hpp"
#include "pple/xml.hpp"

namespace {

using pple::PeImage;
using pple::XmlNode;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string normalise(const std::string& s) {
    std::string out = lower(s);
    for (char& c : out)
        if (c == '/') c = '\\';
    return out;
}

std::string replace_extension(const std::string& name, const std::string& new_ext) {
    const std::size_t dot = name.find_last_of('.');
    const std::size_t slash = name.find_last_of("\\/");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return name;
    return name.substr(0, dot) + new_ext;
}

// Every module is searched in load order, exactly as DllFileMgr iterates its HMODULE array.
struct ModuleSet {
    std::vector<PeImage> modules;
    std::vector<std::string> names;
    std::set<std::string> index;

    bool load(const std::string& path, std::string& error) {
        auto pe = PeImage::load(path, error);
        if (!pe) return false;
        for (const auto& r : pe->resources()) index.insert(r.norm);
        names.push_back(path);
        modules.push_back(std::move(*pe));
        return true;
    }
    bool has(const std::string& name) const { return index.count(normalise(name)) != 0; }

    // Mirrors the loader: as-is, then ".cab"; ".sht" is rewritten to ".jpg" first.
    bool resolve(const std::string& requested, std::string& how) const {
        if (has(requested)) {
            how = "exact";
            return true;
        }
        if (lower(requested).ends_with(".sht")) {
            const std::string stem = replace_extension(requested, "");
            const std::string as_jpg = stem + ".jpg";
            if (has(as_jpg)) {
                how = "sht -> .jpg";
                return true;
            }
            const std::string bmp_cab = stem + ".bmp.cab";
            if (has(bmp_cab)) {
                how = "sht -> .bmp.cab";
                return true;
            }
            if (has(requested + ".cab")) {
                how = "sht -> .sht.cab";
                return true;
            }
        }
        if (has(requested + ".cab")) {
            how = "name + .cab";
            return true;
        }
        return false;
    }
};

struct PathStats {
    std::size_t count = 0;
    std::map<std::string, std::size_t> attributes;
    std::vector<std::string> samples;
};

// Text that looks like a resource path rather than a number or a sentence.
bool looks_like_reference(const std::string& text) {
    if (text.size() < 4 || text.size() > 200) return false;
    if (text.find(' ') != std::string::npos) return false;
    const std::size_t dot = text.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= text.size()) return false;
    const std::string ext = lower(text.substr(dot + 1));
    static const std::set<std::string> known{"xml", "sht", "jpg", "jpeg", "bmp", "png",
                                             "cab", "wma", "txt"};
    return known.count(ext) != 0;
}

void walk(const XmlNode& node, std::map<std::string, PathStats>& stats, const std::string& prefix,
          std::size_t sample_limit) {
    const std::string path = prefix.empty() ? node.name : prefix + "/" + node.name;
    PathStats& slot = stats[path];
    ++slot.count;
    for (const auto& a : node.attributes) {
        ++slot.attributes[a.name];
        if (slot.samples.size() < sample_limit && !a.value.empty()) {
            std::string sample = a.name + "=" + a.value;
            if (std::find(slot.samples.begin(), slot.samples.end(), sample) == slot.samples.end())
                slot.samples.push_back(std::move(sample));
        }
    }
    if (!node.text.empty() && slot.samples.size() < sample_limit) {
        std::string sample = node.text;
        if (sample.size() > 60) sample = sample.substr(0, 57) + "...";
        if (std::find(slot.samples.begin(), slot.samples.end(), sample) == slot.samples.end())
            slot.samples.push_back(std::move(sample));
    }
    for (const XmlNode& c : node.children) walk(c, stats, path, sample_limit);
}

struct RefStats {
    std::size_t total = 0;
    std::size_t resolved = 0;
    std::map<std::string, std::size_t> by_method;
    std::map<std::string, std::size_t> unresolved;
};

void collect_refs(const XmlNode& node, const ModuleSet& modules, RefStats& stats) {
    if (!node.text.empty() && looks_like_reference(node.text)) {
        ++stats.total;
        std::string how;
        if (modules.resolve(node.text, how))
            ++stats.resolved, ++stats.by_method[how];
        else
            ++stats.unresolved[node.name + " = " + node.text];
    }
    for (const XmlNode& c : node.children) collect_refs(c, modules, stats);
}

void usage() {
    std::fprintf(stderr,
                 "pple-xml - inventory the Purble Place XML data model\n\n"
                 "  pple-xml <pe>... [--refs] [--examples N] [--path SUBSTR] [--list]\n\n"
                 "  --refs        resolve resource references and report the method used\n"
                 "  --examples N  example values per element path (default 2)\n"
                 "  --path S      only report element paths containing S\n"
                 "  --list        list each XML resource with its root element\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        usage();
        return args.empty() ? 1 : 0;
    }

    std::vector<std::string> files;
    bool do_refs = false;
    bool do_list = false;
    std::size_t examples = 2;
    std::string path_filter;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--refs") do_refs = true;
        else if (a == "--list") do_list = true;
        else if (a == "--examples" && i + 1 < args.size()) examples = std::stoul(args[++i]);
        else if (a == "--path" && i + 1 < args.size()) path_filter = args[++i];
        else if (!a.empty() && a[0] != '-') files.push_back(a);
    }
    if (files.empty()) {
        usage();
        return 1;
    }

    ModuleSet modules;
    for (const std::string& f : files) {
        std::string error;
        if (!modules.load(f, error)) {
            std::fprintf(stderr, "%s: %s\n", f.c_str(), error.c_str());
            return 1;
        }
    }

    std::map<std::string, PathStats> stats;
    std::map<std::string, std::size_t> roots;
    RefStats refs;
    std::size_t xml_count = 0, parsed = 0;
    std::vector<std::string> failures;

    for (std::size_t m = 0; m < modules.modules.size(); ++m) {
        const PeImage& pe = modules.modules[m];
        for (const auto& r : pe.resources()) {
            const std::vector<std::uint8_t> payload = pe.read(r);
            if (payload.size() < 5) continue;
            const bool looks_xml = payload[0] == '<' || (payload[0] == 0xEF && payload[1] == 0xBB) ||
                                   (payload[0] == 0xFF && payload[1] == 0xFE);
            if (!looks_xml) continue;
            ++xml_count;
            XmlNode root;
            std::string error;
            if (!pple::xml_parse(payload, root, error)) {
                if (failures.size() < 10) failures.push_back(r.name + ": " + error);
                continue;
            }
            ++parsed;
            ++roots[root.name];
            if (do_list)
                std::printf("%-52s root=%-14s %u bytes\n", r.name.c_str(), root.name.c_str(), r.size);
            walk(root, stats, "", examples);
            if (do_refs) collect_refs(root, modules, refs);
        }
    }

    std::printf("modules        : %zu\n", modules.modules.size());
    std::printf("XML resources  : %zu, parsed: %zu\n", xml_count, parsed);
    for (const std::string& f : failures) std::printf("  FAIL %s\n", f.c_str());

    std::printf("\nroot elements:\n");
    for (const auto& [name, count] : roots) std::printf("  %-18s %5zu\n", name.c_str(), count);

    std::printf("\nelement paths (%zu distinct):\n", stats.size());
    for (const auto& [path, st] : stats) {
        if (!path_filter.empty() && path.find(path_filter) == std::string::npos) continue;
        std::printf("  %-58s x%-5zu", path.c_str(), st.count);
        if (!st.attributes.empty()) {
            std::printf("  attrs:");
            for (const auto& [name, n] : st.attributes) std::printf(" %s(%zu)", name.c_str(), n);
        }
        std::printf("\n");
        for (const std::string& sample : st.samples) std::printf("      e.g. %s\n", sample.c_str());
    }

    if (do_refs) {
        std::printf("\nresource references: %zu seen, %zu resolved, %zu unresolved\n", refs.total,
                    refs.resolved, refs.total - refs.resolved);
        std::printf("  resolution methods:\n");
        for (const auto& [how, count] : refs.by_method) std::printf("    %-28s %5zu\n", how.c_str(), count);
        if (!refs.unresolved.empty()) {
            std::printf("  unresolved examples:\n");
            std::size_t shown = 0;
            for (const auto& [what, count] : refs.unresolved) {
                std::printf("    x%-3zu %s\n", count, what.c_str());
                if (++shown >= 15) break;
            }
        }
    }
    return 0;
}
