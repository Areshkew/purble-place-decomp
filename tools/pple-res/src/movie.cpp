#include "pple/movie.hpp"

#include <algorithm>
#include <cstdlib>

#include "pple/xml.hpp"

namespace pple {
namespace {

// The layout mixes <X>/<x> and <Root>/<root>, so every lookup is case-insensitive.
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const XmlNode* child_ci(const XmlNode& node, const std::string& name) {
    const std::string want = lower(name);
    for (const XmlNode& c : node.children)
        if (lower(c.name) == want) return &c;
    return nullptr;
}

bool attr_ci(const XmlNode& node, const std::string& name, std::string& out) {
    const std::string want = lower(name);
    for (const XmlAttribute& a : node.attributes) {
        if (lower(a.name) == want) {
            out = a.value;
            return true;
        }
    }
    return false;
}

float attr_float(const XmlNode& node, const std::string& name, float fallback, bool& present) {
    std::string text;
    present = attr_ci(node, name, text);
    if (!present || text.empty()) return fallback;
    return static_cast<float>(std::atof(text.c_str()));
}

int attr_int(const XmlNode& node, const std::string& name, int fallback) {
    std::string text;
    if (!attr_ci(node, name, text) || text.empty()) return fallback;
    return std::atoi(text.c_str());
}

// One property's value at `frame`: the value of the last key at or before it.  This is a step
// function, not an interpolation.
//
// PPMovieInstance::UpdateFrame (0x10008761C) walks the instance's <Frame> list for a key whose
// number is *equal* to the frame the movie is advancing to, and applies only the attributes that
// key carries (`if (*(_DWORD *)(key + 4) == a2)`, then one `if (key->flags & bit)` per property);
// a frame with no matching key changes nothing, and a key that omits an attribute leaves it
// alone.  PurblePlaceMovie::Update (0x100087EF8) calls UpdateFrame once for every frame index
// from the last applied one to the current one, which is what makes an omitted attribute hold
// its value rather than blend towards the next key.
//
// Interpolating instead smears each change across the whole gap between two keys.  The
// Professor's blink and mouth tracks are many copies of one sprite that snap between alpha 0 and
// 100 on a single frame, so blending leaves several *different* images partly opaque on top of
// each other: the open eye is mixed with the solid blue closed lid and loses its pupil, and the
// exit's one-frame blur flash becomes a slow glow over a Professor who is still fading out.
float sample(const MoviePart& part, int frame, bool MovieKey::*has, float MovieKey::*value,
             float fallback) {
    const MovieKey* held = nullptr;
    for (const MovieKey& k : part.keys) {
        if (!(k.*has) || k.frame > frame) continue;
        if (held == nullptr || k.frame >= held->frame) held = &k;
    }
    return held != nullptr ? held->*value : fallback;
}

}  // namespace

bool movie_load(std::span<const std::uint8_t> data,
                const std::function<bool(const std::string&, std::vector<std::uint8_t>&)>& fetch,
                Movie& movie, std::string& error) {
    movie = Movie{};
    XmlNode root;
    if (!xml_parse(data, root, error)) return false;
    movie.name = root.attr("Name");

    const XmlNode* animations = child_ci(root, "Animations");
    if (animations == nullptr) {
        error = "no <Animations> element";
        return false;
    }

    // <Images><ImageDef name= xml=/>  describes every part the library can name.
    std::vector<std::pair<std::string, std::string>> defs;
    if (const XmlNode* images = child_ci(*animations, "Images")) {
        for (const XmlNode& def : images->children) {
            if (lower(def.name) != "imagedef") continue;
            defs.emplace_back(def.attr("name"), def.attr("xml"));
        }
    }

    const XmlNode* library = child_ci(*animations, "Library");
    if (library == nullptr) {
        error = "no <Library> element";
        return false;
    }
    std::string frames_text;
    if (attr_ci(*library, "frameCount", frames_text)) movie.frame_count = std::atoi(frames_text.c_str());
    std::string fps_text;
    if (attr_ci(*library, "fps", fps_text)) movie.fps = static_cast<float>(std::atof(fps_text.c_str()));
    movie.frame_count = std::max(1, movie.frame_count);

    for (const XmlNode& instance : library->children) {
        if (lower(instance.name) != "imageinstance") continue;
        MoviePart part;
        part.name = instance.attr("name");
        std::string part_fps;
        if (attr_ci(instance, "fps", part_fps)) part.fps = static_cast<float>(std::atof(part_fps.c_str()));

        for (const auto& [def_name, def_xml] : defs) {
            if (def_name == part.name) {
                part.sprite_xml = def_xml;
                break;
            }
        }
        if (part.sprite_xml.empty()) {
            movie.problems.push_back("part \"" + part.name + "\" has no <ImageDef>");
        } else {
            std::vector<std::uint8_t> payload;
            if (!fetch(part.sprite_xml, payload)) {
                movie.problems.push_back("part \"" + part.name + "\": " + part.sprite_xml + " not found");
            } else {
                std::string node_error;
                if (node_definition_load(payload, part.sprite, node_error, 0)) {
                    part.resolved = true;
                } else {
                    movie.problems.push_back("part \"" + part.name + "\": " + node_error);
                }
            }
        }

        for (const XmlNode& frame : instance.children) {
            if (lower(frame.name) != "frame") continue;
            MovieKey key;
            key.frame = attr_int(frame, "num", 0);
            key.x = attr_float(frame, "x", key.x, key.has_x);
            key.y = attr_float(frame, "y", key.y, key.has_y);
            key.rotation = attr_float(frame, "rotation", key.rotation, key.has_rotation);
            key.xscale = attr_float(frame, "xscale", key.xscale, key.has_xscale);
            key.yscale = attr_float(frame, "yscale", key.yscale, key.has_yscale);
            key.alpha = attr_float(frame, "alpha", key.alpha, key.has_alpha);
            part.keys.push_back(key);
        }
        std::stable_sort(part.keys.begin(), part.keys.end(),
                         [](const MovieKey& a, const MovieKey& b) { return a.frame < b.frame; });
        movie.parts.push_back(std::move(part));
    }

    if (movie.parts.empty()) {
        error = "<Library> has no <ImageInstance>";
        return false;
    }
    return true;
}

float movie_frame_rate(const Movie& movie, const MoviePart& part) {
    if (part.fps > 0.0f) return part.fps;
    if (movie.fps > 0.0f) return movie.fps;
    // PurblePlaceMovie::InitFromXml (0x100087A89): a library with no @FPS logs
    // "No FPS found for this movie, defaulting to 30 fps" and stores 30.0f (0x41F00000).  The
    // Concentration burst movies (ShamBurstMovie.xml and the other three) carry only
    // frameCount, so this is the rate the game plays them at.
    return 30.0f;
}

void movie_evaluate(const Movie& movie, int frame, std::vector<MoviePlacement>& out) {
    out.clear();
    const int clamped = std::max(0, std::min(frame, movie.frame_count - 1));
    for (const MoviePart& part : movie.parts) {
        MoviePlacement p;
        p.part = &part;
        p.x = sample(part, clamped, &MovieKey::has_x, &MovieKey::x, 0.0f);
        p.y = sample(part, clamped, &MovieKey::has_y, &MovieKey::y, 0.0f);
        p.rotation = sample(part, clamped, &MovieKey::has_rotation, &MovieKey::rotation, 0.0f);
        p.xscale = sample(part, clamped, &MovieKey::has_xscale, &MovieKey::xscale, 100.0f) / 100.0f;
        p.yscale = sample(part, clamped, &MovieKey::has_yscale, &MovieKey::yscale, 100.0f) / 100.0f;
        p.alpha = sample(part, clamped, &MovieKey::has_alpha, &MovieKey::alpha, 100.0f) / 100.0f;
        out.push_back(p);
    }
}

}  // namespace pple
