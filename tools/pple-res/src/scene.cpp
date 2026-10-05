#include "pple/scene.hpp"
#include "pple/mui.hpp"

#include <algorithm>
#include <cstdlib>

namespace pple {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The layout is inconsistent about case: <X> and <x> both appear, as do <Frames> and
// <frames>.  Look up case-insensitively.
const XmlNode* child_ci(const XmlNode& node, const std::string& name) {
    const std::string want = lower(name);
    for (const XmlNode& c : node.children)
        if (lower(c.name) == want) return &c;
    return nullptr;
}

std::string child_text_ci(const XmlNode& node, const std::string& name) {
    const XmlNode* c = child_ci(node, name);
    return c == nullptr ? std::string{} : c->text;
}

float to_float(const std::string& s, float fallback = 0.0f) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return end == s.c_str() ? fallback : v;
}

std::uint32_t parse_colour(const std::string& s, std::uint32_t fallback) {
    if (s.size() != 8) return fallback;
    return static_cast<std::uint32_t>(std::strtoul(s.c_str(), nullptr, 16));
}

// Frame rectangle within a sheet, from the node definition's animation block.
void read_animation(const XmlNode& animation, SceneItem& item) {
    item.sheet = child_text_ci(animation, "Texture");
    item.src_x = to_float(child_text_ci(animation, "SheetOffsetX"));
    item.src_y = to_float(child_text_ci(animation, "SheetOffsetY"));
    item.frames = std::max(1, static_cast<int>(to_float(child_text_ci(animation, "FrameCount"), 1.0f)));
    item.per_row = std::max(1, static_cast<int>(to_float(child_text_ci(animation, "FramesPerRow"), 1.0f)));
    const std::string prepped = child_text_ci(animation, "PreppedForFilter");
    item.prepped = !prepped.empty() && to_float(prepped) > 0.0f;
}
void walk(const XmlNode& node, float offset_x, float offset_y, float scale, Scene& scene);

void emit_instance(const XmlNode& node, const XmlNode& desc, float x, float y, float scale, Scene& scene) {
    SceneItem item;
    item.instance = node.name;
    item.name = node.attr("Name");
    item.x = x;
    item.y = y;
    item.scale = scale;
    item.w = to_float(child_text_ci(node, "W"));
    item.h = to_float(child_text_ci(node, "H"));
    item.has_position = child_ci(node, "X") != nullptr || child_ci(node, "x") != nullptr ||
                        child_ci(node, "Y") != nullptr || child_ci(node, "y") != nullptr;
    item.alpha = static_cast<int>(to_float(child_text_ci(desc, "Alpha"), 255.0f));

    if (const XmlNode* sprite = child_ci(desc, "Sprite")) {
        item.kind = NodeKind::Sprite;
        item.ref = sprite->text;
    } else if (const XmlNode* button = child_ci(desc, "Button")) {
        item.kind = NodeKind::Button;
        item.ref = button->text;
    } else if (const XmlNode* movie = child_ci(desc, "Movie")) {
        item.kind = NodeKind::Movie;
        item.ref = movie->text;
    } else if (child_ci(desc, "FontName") != nullptr || child_ci(desc, "Text") != nullptr) {
        item.kind = NodeKind::Label;
        item.font_name = localized_default(child_text_ci(desc, "FontName"));
        item.font_size = to_float(localized_default(child_text_ci(desc, "FontSize")));
        item.text = localized_default(child_text_ci(desc, "Text"));
        item.colour = parse_colour(child_text_ci(desc, "Color"), 0xFFFFFFFFu);
        item.justify = static_cast<int>(to_float(child_text_ci(desc, "Justify")));
        scene.items.push_back(std::move(item));
        return;
    } else {
        item.kind = NodeKind::Unknown;
        item.ref = "(desc with no known reference)";
        scene.items.push_back(std::move(item));
        return;
    }

    // Sprite/Button/Movie: pull the sheet and frame rectangle out of the referenced node
    // definition.  A movie is a composed animation and is left unresolved for now.
    if (item.kind == NodeKind::Movie) {
        item.sheet.clear();
        scene.items.push_back(std::move(item));
        return;
    }
    scene.items.push_back(std::move(item));
}

void walk(const XmlNode& node, float offset_x, float offset_y, float scale, Scene& scene) {
    float x = offset_x + to_float(child_text_ci(node, "X")) + to_float(child_text_ci(node, "x"));
    // Guard against both spellings being present at once.
    if (child_ci(node, "X") != nullptr && child_ci(node, "x") != nullptr)
        x = offset_x + to_float(child_text_ci(node, "X"));
    float y = offset_y + to_float(child_text_ci(node, "Y")) + to_float(child_text_ci(node, "y"));
    if (child_ci(node, "Y") != nullptr && child_ci(node, "y") != nullptr)
        y = offset_y + to_float(child_text_ci(node, "Y"));

    float local_scale = scale;
    const std::string scale_text = child_text_ci(node, "scale");
    if (!scale_text.empty()) local_scale = scale * to_float(scale_text) / 100.0f;

    const XmlNode* desc = child_ci(node, "Desc");
    if (desc != nullptr) {
        emit_instance(node, *desc, x, y, local_scale, scene);
        return;  // a described instance does not also contain instances
    }

    // No <Desc>: this is a container.  Record its placement so the engine's own re-parenting
    // (apply_container_offsets) and the viewer can find it by name.
    if (!node.name.empty()) {
        SceneItem container;
        container.instance = node.name;
        container.name = node.attr("Name");
        container.x = x;
        container.y = y;
        container.scale = local_scale;
        scene.containers.push_back(std::move(container));
    }

    // <Background>path</Background> is shorthand for a sprite instance with no Desc.  It is
    // a sibling of the other instances, so emit it and keep walking rather than returning.
    if (const XmlNode* background = child_ci(node, "Background")) {
        if (!background->text.empty() && background->text.find(".xml") != std::string::npos) {
            SceneItem item;
            item.instance = node.name + "/Background";
            item.kind = NodeKind::Sprite;
            item.ref = background->text;
            item.x = x;
            item.y = y;
            item.scale = local_scale;
            scene.items.push_back(std::move(item));
        }
    }

    for (const XmlNode& child : node.children) walk(child, x, y, local_scale, scene);
}

}  // namespace

// Mirrors NodeSprite::UpdateCurrentUVs (0x100094552): with <PreppedForFilter> set, the frame
// sits one pixel inside its slot and consecutive frames are strided by SourceWidth+2.
void sprite_frame_rect(const SceneItem& item, int frame, float& x, float& y, float& w, float& h) {
    const int last = std::max(1, item.frames) - 1;
    const int index = frame < 0 ? 0 : (frame > last ? last : frame);
    const float p = item.prepped ? 1.0f : 0.0f;
    const int per_row = std::max(1, item.per_row);
    x = item.src_x + p + static_cast<float>(index % per_row) * (item.src_w + 2.0f * p);
    y = item.src_y + p + static_cast<float>(index / per_row) * (item.src_h + 2.0f * p);
    w = item.src_w;
    h = item.src_h;
}

const char* node_kind_name(NodeKind kind) {
    switch (kind) {
        case NodeKind::Sprite: return "Sprite";
        case NodeKind::Button: return "Button";
        case NodeKind::Label: return "Label";
        case NodeKind::Movie: return "Movie";
        default: return "Unknown";
    }
}

std::string localized_default(const std::string& value) {
    // "|id|context|text//comment" -> the .mui string for id, or the embedded English text when
    // there is no localiser or the id is absent.  NodeLabel::SetText (0x1000A1018) only calls
    // LocalizeMessage when the value starts with '|', which is the same test.
    if (value.empty() || value[0] != '|') return value;
    const std::size_t first = value.find('|', 1);
    if (first == std::string::npos) return value;
    const std::size_t second = value.find('|', first + 1);
    if (second == std::string::npos) return value;
    std::string text = value.substr(second + 1);
    const std::size_t comment = text.find("//");
    if (comment != std::string::npos) text = text.substr(0, comment);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
    if (Localiser resolve = localiser()) {
        const int id = std::atoi(value.substr(1, first - 1).c_str());
        if (id > 0) return resolve(id, text, nullptr);
    }
    return text;
}

bool node_definition_load(std::span<const std::uint8_t> data, SceneItem& item, std::string& error,
                          int state) {
    XmlNode root;
    if (!xml_parse(data, root, error)) return false;
    const std::string type = root.attr("Type");
    if (type == "Sprite") item.kind = NodeKind::Sprite;
    else if (type == "Button") item.kind = NodeKind::Button;
    else if (type == "Label") item.kind = NodeKind::Label;
    else item.kind = NodeKind::Unknown;
    item.sheet_class = root.child_text("SheetClass");
    item.src_w = to_float(root.child_text("SourceWidth"));
    item.src_h = to_float(root.child_text("SourceHeight"));
    item.reg_x = to_float(root.child_text("RegistrationX"));
    item.reg_y = to_float(root.child_text("RegistrationY"));
    // A button carries one <Animation> per visual state.  The engine's current source index
    // starts at 0 (NodeSprite ctor 0x100094021) and is moved to 1 on <enter> and 2 on <down>
    // (NodeButton::HandleEvent 0x1000AAC92 / 0x1000AAE1C), so state 0 is what an untouched
    // button draws.
    int animations = 0;
    for (const XmlNode& c : root.children) {
        if (c.name != "Animation") continue;
        if (animations == state) read_animation(c, item);
        ++animations;
    }
    item.states = std::max(1, animations);
    if (animations == 0) {
        error = "node definition has no <Animation>";
        return false;
    }
    if (state >= animations) {
        error = "node definition has no <Animation> at index " + std::to_string(state) + " (" +
                std::to_string(animations) + " present)";
        return false;
    }
    return true;
}

bool scene_load(std::span<const std::uint8_t> data,
                const std::function<bool(const std::string&, std::vector<std::uint8_t>&)>& fetch,
                Scene& scene, std::string& error) {
    XmlNode root;
    if (!xml_parse(data, root, error)) return false;
    scene.name = root.attr("Name");
    walk(root, 0.0f, 0.0f, 1.0f, scene);

    // Resolve every referenced node definition now, so the caller only has to deal with
    // sheets.
    for (SceneItem& item : scene.items) {
        if (item.ref.empty() || item.kind == NodeKind::Label || item.kind == NodeKind::Movie) continue;
        std::vector<std::uint8_t> definition;
        if (!fetch(item.ref, definition)) {
            scene.problems.push_back("missing node definition: " + item.ref);
            item.kind = NodeKind::Unknown;
            continue;
        }
        SceneItem resolved;
        std::string node_error;
        if (!node_definition_load(definition, resolved, node_error)) {
            scene.problems.push_back(item.ref + ": " + node_error);
            item.kind = NodeKind::Unknown;
            continue;
        }
        // Keep the instance's placement; take the appearance from the definition.
        const float x = item.x, y = item.y, scale = item.scale;
        const int alpha = item.alpha;
        const bool has_position = item.has_position;
        const std::string instance = item.instance, name = item.name;
        const std::string ref = item.ref;
        item = resolved;
        item.x = x;
        item.y = y;
        item.scale = scale;
        item.alpha = alpha;
        item.has_position = has_position;
        item.instance = instance;
        item.name = name;
        item.ref = ref;
    }
    return true;
}

// See the declaration: the engine re-parents the HUD by lookup, the document lists the
// elements as siblings.  Container offsets are read from the container instances the document
// does carry (they are plain positioned elements with no <Desc>, so the walk keeps them).
void apply_container_offsets(Scene& scene) {
    struct Rule {
        const char* element;
        const char* container;
    };
    static const Rule kRules[] = {
        {"TimeBarBkg", "TimeBarContainer"},
        {"TimeBarGreen", "TimeBarContainer"},
        {"TimeBarRed", "TimeBarContainer"},
        {"SneakPeekBackground", "SneakPeekContainer"},
        {"SneakPeekTokenAnimate", "SneakPeekContainer"},
        {"SneakPeekToken", "SneakPeekContainer"},
        {"SneakSparks", "SneakPeekContainer"},
        {"JarBase", "JarContainer"},
        {"JarBkg", "JarContainer"},
        {"JarFg", "JarContainer"},
        {"ProgressMeterIndicators", "ProgressMeterContainer"},
    };
    for (const Rule& rule : kRules) {
        float cx = 0.0f, cy = 0.0f;
        bool found = false;
        for (const SceneItem& container : scene.containers) {
            const std::string& key =
                container.name.empty() ? container.instance : container.name;
            if (key != rule.container) continue;
            cx = container.x;
            cy = container.y;
            found = true;
            break;
        }
        if (!found) continue;
        for (SceneItem& item : scene.items) {
            const std::string& key = item.name.empty() ? item.instance : item.name;
            if (key != rule.element) continue;
            item.x += cx;
            item.y += cy;
        }
    }
}

}  // namespace pple
