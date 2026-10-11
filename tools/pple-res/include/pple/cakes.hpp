// The Comfy Cakes rules, with no SDL.  Everything here comes from the binary:
//
//   the difficulty variables      COMFYCAKESVARS.XML, bound by TweakVars::Bind in
//                                 ComfyCakesGame::Init (0x100059F9C)
//   the layer table               UI\ComfyCakes\Layers.xml
//   the layer operation matrix    LayerOperations::GetLayerOperation (0x10006F1F0), a 6x6
//                                 table of ints at 0x100014430 indexed
//                                 table[6 * current + tool] (row = the current top layer's
//                                 station, column = the station of the tool being used)
//   the station numbers           each tool document's <Station>: CakePanTool 1, BatterTool 0,
//                                 DecorationTool 2, FillingTool 3, FinalTouchTool 4,
//                                 IcingTool 5 -- the same numbering Cake::CanAddLayer's
//                                 illegal-move tips use (0x1000565B5)
//   the belt geometry             ConveyorBelt::SetCakePosition (0x100063074): a cake at slot p
//                                 sits at (98*p - 66, 29) inside the belt container, and slot 0
//                                 is the off-belt position (66, -115)
//   the round and score           ComfyCakesRoundData::ToXml (0x100061468) for the field layout,
//                                 ComputeScore (0x10005CC4C) for the arithmetic and
//                                 CheckEndCondition (0x10005C7E8) for the end rule
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "pple/xml.hpp"

namespace pple {

// One entry of LAYERS.xml.  A layer is either a plain <Layer><Delta/><Sprite/></Layer> or a
// <Layer><Shape>...</Shape><Shape>...</Shape><Shape>...</Shape></Layer>, where each <Shape> names
// the sprite for one of the three pan shapes (round, rectangle, heart).
struct CakeLayerDef {
    int delta = 0;
    std::string sprite;                 // the single-sprite form
    std::vector<std::string> shapes;    // one per pan shape, in round/rectangle/heart order
    std::vector<int> shape_deltas;      // each Shape owns its own height, not the last Shape's
    int delta_for(int shape) const {
        if (shape_deltas.empty()) return delta;
        const std::size_t at = shape < 0 || static_cast<std::size_t>(shape) >= shape_deltas.size()
                                  ? 0 : static_cast<std::size_t>(shape);
        return shape_deltas[at];
    }
};

// The per-difficulty variables.  The field offsets are the TweakVars::Bind targets in
// ComfyCakesGame::Init.
struct CakeVars {
    int right_cake_points = 0;
    int wrong_cake_points = 0;
    int moves_percent[11] = {};      // 0, 10, ... 100 percent
    int cakes_needed = 0;
    int missed_cakes_allowed = 0;
    float belt_slide_time = 0.0f;
    float num_slides_to_add_cake = 0.0f;
    int min_layers = 1, max_layers = 1;
    int min_toppings = 1, max_toppings = 1;
    float num_time_to_advance_belt = 0.0f;
    float num_time_to_add_cake = 0.0f;
    float num_acceleration_to_add_cake_time = 0.0f;
    float num_min_time_to_add_cake = 0.0f;
    bool time_attack_allowed = false;
};

// One layer on a cake: which tool put it there, which entry of LAYERS.xml it is, which pan shape
// it was built for and which way round the decoration sits (Cake::PutLayerOnTop's orientation).
struct CakeLayer {
    int station = -1;    // 0 batter, 1 pan, 2 decoration, 3 filling, 4 final touch, 5 icing
    int index = -1;      // the LAYERS.xml entry
    int shape = -1;      // 0 round, 1 rectangle, 2 heart; -1 when the layer is shape-independent
    int orientation = 0; // 0..3, the quarter turn a decoration or final touch was placed at
};

// A cake is an ordered stack of layers, which is how Cake::Compare walks it.
struct Cake {
    std::vector<CakeLayer> layers;
    int height_delta = 0;  // the sum of the layer deltas, Cake+0x14 in the engine
    // The pan shape the cake was built in, -1 until a pan has been used.  Cake::AddLayer's
    // replace path passes the removed layer's shape (Cake+0x28) on to the new one, so the pan
    // sets the shape and the batter that replaces it inherits it.
    int shape = -1;
    // The raw sequence of tool uses that produced this cake, which is exactly what a player has
    // to replay.  ComfyCakesGame::GenerateCorrectCake calls Cake::AddLayer and ignores its return
    // value, so a refused layer is absent from `layers` but still a step in the sequence: the
    // pan before a second layer's batter is one of those, and without it a multi-layer cake
    // cannot be rebuilt.
    std::vector<CakeLayer> moves;
    // Engine flags the reconstruction needs for the round flow.
    bool wrong = false;     // Cake+0x10, set by ComfyCakesGame::OnToolUsed (0x10005C6F5)
    bool matched = false;   // Cake+0x11
    int station = -1;       // the belt slot the cake occupies
};

// How a tool use is answered.  The engine's Cake::AddLayer returns through
// Tool::UseTool's two branches (0x1000752ED): the add animation or the remove animation.
enum class CakeUse { Refused, Added, Replaced, Stripped };

// The operation matrix, verbatim from 0x100014430.  Row = the current top layer's station,
// column = the station of the tool being used.  See LayerOperations::GetLayerOperation.
//   0 refused, 1 put on top, 2 replace the top, 3 strip to a base, 4 strip the decorations,
//   5 the glaze/sugar path, 6 the pan-on-an-empty-platter path
inline constexpr int kLayerOperation[6][6] = {
    //  bat  pan  dec  fill fin  icing            (tool being used)
    {
        6, 1, 1, 1, 5, 1
    },                                                                       // cur batter
    {
        2, 6, 0, 0, 0, 0
    },                                                                       // cur pan
    {
        0, 0, 0, 0, 0, 0
    },                                                                       // cur decoration
    {
        0, 0, 0, 0, 0, 0
    },                                                                       // cur filling
    {
        0, 2, 4, 3, 5, 3
    },                                                                       // cur final touch
    {
        0, 1, 1, 3, 5, 3
    },                                                                       // cur icing
};

// The result Cake::Compare returns.  0 is an exact match, 3 is "the target is longer", 4 is "this
// one is longer", 5 is a mismatch, 2 is a decoration or final touch in the wrong orientation and
// 1 is a decoration of the wrong kind.
enum class CakeCompare {
    Exact = 0,
    WrongDecorationKind = 1,
    WrongOrientation = 2,
    TargetLonger = 3,
    SourceLonger = 4,
    Mismatch = 5,
};

// The belt.  ConveyorBelt::Init clears nine slots (0x10006679) and GetCake bounds-checks against
// the slot count (0x100063038).
constexpr int kCakeBeltSlots = 9;

class CakeModel {
    public:
        // The three difficulties, indexed 0 Beginner, 1 Intermediate, 2 Expert.
        static constexpr int kDifficultyCount = 3;

        bool load_vars(const std::vector<std::uint8_t>& xml, std::string& error);
        bool load_layers(const std::vector<std::uint8_t>& xml, std::string& error);

        const CakeVars& vars(int difficulty) const {
            return vars_[difficulty < 0 || difficulty >= kDifficultyCount ? 0 : difficulty];
        }
        const std::vector<CakeLayerDef>& layer_defs() const {
            return layers_;
        }
        bool layers_ok() const {
            return layers_ok_ && vars_ok_;
        }

        // The LAYERS.xml entry a tool button names.  Each <ButtonTool> carries <Type> with the
        // entry's index (CakePanToolRoundButton 3, BatterToolChocolateButton 0, ...).
        const CakeLayerDef* layer(int index) const {
            if (index < 0 || index >= static_cast<int>(layers_.size())) return nullptr;
            return &layers_[static_cast<std::size_t>(index)];
        }

        // ComfyCakesGame::GenerateCorrectCake (0x100060460).  `rng` supplies the engine's
        // rand() sequence in call order, so a fixed stream reproduces a fixed target.
        Cake generate_correct(int difficulty, const std::function<int()>& rng) const;
        // What a tool button does.  Returns the answer and, for Added/Replaced/Stripped, the
        // layer that was put on top.
        CakeUse use_tool(Cake& cake, int station, int index, CakeLayer& added,
                         const std::function<int()>& rng) const;
        CakeCompare compare(const Cake& a, const Cake& b) const;

        // ConveyorBelt::SetCakePosition (0x100063074), inside the belt container.
        static void belt_position(int slot, float& x, float& y) {
            if (slot <= 0) {
                x = 66.0f;
                y = -115.0f;
            } else {
                x = 98.0f * static_cast<float>(slot) - 66.0f;
                y = 29.0f;
            }
        }

    private:
        CakeVars vars_[kDifficultyCount];
        std::vector<CakeLayerDef> layers_;
        bool vars_ok_ = false;
        bool layers_ok_ = false;
        // A layer's LAYERS.xml <Delta>.
        int delta_of(const CakeLayer& value) const {
            const CakeLayerDef* def = layer(value.index);
            return def == nullptr ? 0 : def->delta_for(value.shape);
        }
        void pop_delta(Cake& cake) const {
            cake.height_delta -= delta_of(cake.layers.back());
            cake.layers.pop_back();
        }
};

}  // namespace pple
