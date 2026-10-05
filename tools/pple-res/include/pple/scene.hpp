// Scene-graph loader for the game's UI layout XML.
//
// Two levels, as the data uses them:
//   * a node definition, <Root Type="Sprite|Button|...">, holding one or more
//     <Animation> states, SourceWidth/SourceHeight, Registration and access strings
//   * a scene, whose children are named instances carrying a position and a <Desc> that
//     references a node definition by path (or inline label properties)
//
// Container elements that carry a position but no <Desc> act as offsets for their children,
// so the walk recurses with accumulated offsets.  That is how <JarContainer> and
// <SneakPeekTokens> work.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "pple/xml.hpp"

namespace pple {

enum class NodeKind { Sprite, Button, Label, Movie, Unknown };

const char* node_kind_name(NodeKind kind);

struct SceneItem {
    std::string instance;  // element name, e.g. "NodeButton"
    std::string name;      // Name attribute, if present
    NodeKind kind = NodeKind::Unknown;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float scale = 1.0f;
    int alpha = 255;
    // True when the instance element itself carried an X/x or Y/y child.  A <Desc><Movie> with
    // none is an animation slot the game attaches to a positioned node by name (for example
    // ProfessorIdleAnimation next to ProfessorContainer at 295,70), not something the scene
    // document places.
    bool has_position = false;

    // Sprite/Button
    std::string ref;        // value of <Sprite>/<Button>/<Movie>
    std::string sheet;      // <Texture> from the node definition
    float src_x = 0.0f;     // frame 0 within the sheet
    float src_y = 0.0f;
    float src_w = 0.0f;
    float src_h = 0.0f;
    int frames = 1;
    int per_row = 1;
    int states = 1;         // number of <Animation> entries in the node definition
    // <PreppedForFilter>1</PreppedForFilter> means the frame sits inside a 1-pixel gutter in
    // the sheet, so the sheet offset gains 1 and the per-frame stride gains 2.  Read by
    // NodeSprite::Load 0x100093E63 and applied in NodeSprite::UpdateCurrentUVs 0x100094552.
    bool prepped = false;
    float reg_x = 0.0f;
    float reg_y = 0.0f;
    std::string sheet_class;

    // Label
    std::string font_name;
    std::string text;
    float font_size = 0.0f;
    std::uint32_t colour = 0xFFFFFFFFu;  // AARRGGBB
    int justify = 0;
};

struct Scene {
    std::string name;
    std::vector<SceneItem> items;
    // Positioned elements that carry no <Desc>: plain containers such as /TimeBarContainer or
    // /PlayArea's parents.  They place their children by name at run time (see
    // apply_container_offsets), so the positions have to be reachable.
    std::vector<SceneItem> containers;
    std::vector<std::string> problems;
};

// Loads a node definition (a <Root Type=...> document) into one SceneItem.  `state` selects
// which <Animation> provides the frame (0 is what an untouched button draws).
bool node_definition_load(std::span<const std::uint8_t> data, SceneItem& item, std::string& error,
                          int state = 0);

// Frame rectangle inside the sheet, exactly as NodeSprite::UpdateCurrentUVs computes it:
//   x = SheetOffsetX + p + (frame % FramesPerRow) * (SourceWidth  + 2p)
//   y = SheetOffsetY + p + (frame / FramesPerRow) * (SourceHeight + 2p)
//   w = SourceWidth, h = SourceHeight            with p = 1 when <PreppedForFilter> is set
// The frame index is clamped the way the engine clamps it (frame >= FrameCount lands on the
// last frame).
void sprite_frame_rect(const SceneItem& item, int frame, float& x, float& y, float& w, float& h);

// Loads a scene.  `fetch` returns the bytes of a resource by name, so the caller owns
// resource resolution.
bool scene_load(std::span<const std::uint8_t> data,
                const std::function<bool(const std::string&, std::vector<std::uint8_t>&)>& fetch,
                Scene& scene, std::string& error);

// "|42695|CON_UI|New Game//comment" -> "New Game"
std::string localized_default(const std::string& value);

// The Concentration document lists its HUD elements as root-level siblings, but the engine
// re-parents each one into a container it looks up by path in the object's own InitUI:
//
//   Board::InitUI        0x1000891A8  -> a "Base" it creates, then /PlayArea and the cards
//   TimeBar::InitUI      0x10008517C  -> /TimeBarContainer, then /TimeBarBkg and the segments
//   SneakPeakArea::InitUI 0x1000849C0 -> /SneakPeekContainer, then the peek background/tokens
//   Jar::InitUI          0x100083218  -> /JarContainer, then /JarBase, /JarBkg, /JarFg
//   ProgressMeter::InitUI 0x100084720 -> /ProgressMeterContainer, then the four indicators
//
// A document-order walk therefore puts every one of them at the wrong place, which is why the
// viewer used to draw TimeBarBkg at 3,21 where the game draws it at 60-(-17)+3,21.  This
// applies the engine's parenting as a post-pass over a loaded scene.  The movie slots are
// skipped: they carry no position and are anchored by name instead.
void apply_container_offsets(Scene& scene);

// The engine's design resolution, taken from the shipped window preference and from
// GameBoard.png / PurblePlaceForeground.png, both of which are exactly 800x496.
inline constexpr float kScreenWidth = 800.0f;
inline constexpr float kScreenHeight = 496.0f;

}  // namespace pple
