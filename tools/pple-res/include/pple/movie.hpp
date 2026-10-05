// Skeletal movie animations.
//
// A <Movie> document is not a sprite list.  It is a library of named part images and a set of
// per-part keyframe tracks:
//
//   <Root><Animations>
//     <Images>
//       <ImageDef name="eye1" xml="UI\Concentration\eye1.xml"/>   ... one per part
//     </Images>
//     <Library frameCount="61">
//       <ImageInstance name="arm2" fps="12">
//         <Frame num="0" xscale="100" yscale="100" x="313" y="342" rotation="-48" alpha="100"/>
//         <Frame num="1"  xscale="99" yscale="99"/>
//         <Frame num="60" xscale="100" yscale="100"/>
//       </ImageInstance>
//     </Library>
//   </Animations></Root>
//
// Each <ImageDef xml=...> points at a <Root Type="Sprite"> node definition, so a part is drawn
// with the same frame-rectangle rule as any other sprite (including the <PreppedForFilter>
// gutter).  A <Frame> sets only the attributes it carries; an attribute it omits keeps the
// value from the previous key, and a property with two or more keys is linearly interpolated
// between them.  `alpha`, `xscale` and `yscale` are percentages.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "pple/scene.hpp"

namespace pple {

// One keyframe as it appears in the document.  A property is "set" only when its attribute is
// present, which is what makes <Frame num="1"/> mean "no change".
struct MovieKey {
    int frame = 0;
    bool has_x = false, has_y = false, has_rotation = false;
    bool has_xscale = false, has_yscale = false, has_alpha = false;
    float x = 0.0f, y = 0.0f, rotation = 0.0f;
    float xscale = 100.0f, yscale = 100.0f, alpha = 100.0f;
};

struct MoviePart {
    std::string name;       // <ImageDef name=...>
    std::string sprite_xml; // <ImageDef xml=...>
    SceneItem sprite;       // the resolved node definition
    bool resolved = false;
    float fps = 0.0f;       // <ImageInstance fps=...>; 0 means "inherit the library's"
    std::vector<MovieKey> keys;
};

struct Movie {
    std::string name;
    int frame_count = 1;
    float fps = 0.0f;
    std::vector<MoviePart> parts;
    std::vector<std::string> problems;
};

// Loads a movie document.  `fetch` returns the bytes of a resource by name so the caller owns
// resource resolution; it is used for the <ImageDef xml=...> parts.
bool movie_load(std::span<const std::uint8_t> data,
                const std::function<bool(const std::string&, std::vector<std::uint8_t>&)>& fetch,
                Movie& movie, std::string& error);

struct MoviePlacement {
    const MoviePart* part = nullptr;
    float x = 0.0f, y = 0.0f;
    float rotation = 0.0f;          // degrees, clockwise
    float xscale = 1.0f, yscale = 1.0f;
    float alpha = 1.0f;             // 0..1
};

// Evaluates every part at `frame`, clamping to [0, frameCount-1].
void movie_evaluate(const Movie& movie, int frame, std::vector<MoviePlacement>& out);

// The library frame rate, falling back to a part's own fps and then to 12.
float movie_frame_rate(const Movie& movie, const MoviePart& part);

}  // namespace pple
