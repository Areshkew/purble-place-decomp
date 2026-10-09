#include "pple/cakes.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace pple {
namespace {

const XmlNode* child_ci(const XmlNode& node, const std::string& name) {
    std::string want = name;
    for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const XmlNode& c : node.children) {
        std::string have = c.name;
        for (char& ch : have) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (have == want) return &c;
    }
    return nullptr;
}

std::string text_ci(const XmlNode& node, const std::string& name) {
    const XmlNode* c = child_ci(node, name);
    return c == nullptr ? std::string{} : c->text;
}

int int_ci(const XmlNode& node, const std::string& name, int fallback) {
    const std::string t = text_ci(node, name);
    if (t.empty()) return fallback;
    return std::atoi(t.c_str());
}

float float_ci(const XmlNode& node, const std::string& name, float fallback) {
    const std::string t = text_ci(node, name);
    if (t.empty()) return fallback;
    return static_cast<float>(std::atof(t.c_str()));
}

bool bool_ci(const XmlNode& node, const std::string& name, bool fallback) {
    const std::string t = text_ci(node, name);
    if (t.empty()) return fallback;
    return t == "true" || t == "1";
}

// The layer's operation-class, which is what LayerOperations::GetLayerOperation is indexed by
// (a layer's +16).  It is not the tool's station: Cake::CanAddLayer's illegal-move tips prove
// 0 is a batter ("IllegalMoveBatterOnBatter" fires when the top layer is class 0) and 1 is a pan
// ("IllegalMoveFillingOnPanOrEmpty" fires when it is class 1), and Cake::Compare's decoration
// branch tests class 2 while its final-touch branch tests 4.  Filling shares the batter's class 0,
// which is why a filling sits under icing, and station 3 is never a row of the matrix at all --
// rows 2 and 3 of the shipped table are the two all-zero rows, so only class 3 is unreachable.
int layer_class(int station) {
    switch (station) {
        case 0: return 0;  // batter
        case 1: return 1;  // pan
        case 2: return 2;  // decoration
        case 3: return 0;  // filling
        case 4: return 4;  // final touch
        case 5: return 5;  // icing
        default: return -1;
    }
}

// The layer's station, from its LAYERS.xml entry: 0..2 batter, 3..5 pan, 6..9 decoration,
// 10..12 filling, 13/14 final touch, 15..17 icing.
int station_for_index(int index) {
    if (index <= 2) return 0;
    if (index <= 5) return 1;
    if (index <= 9) return 2;
    if (index <= 12) return 3;
    if (index <= 14) return 4;
    return 5;
}

}  // namespace

bool CakeModel::load_vars(const std::vector<std::uint8_t>& xml, std::string& error) {
    XmlNode root;
    if (!xml_parse(xml, root, error)) return false;
    // The document repeats every variable with a difficulty prefix.
    static const char* kPrefix[3] = {
        "Beginner", "Intermediate", "Expert"
    };
    for (int d = 0; d < 3; ++d) {
        CakeVars& v = vars_[d];
        const std::string p = kPrefix[d];
        v.right_cake_points = int_ci(root, p + "RightCakePoints", 0);
        v.wrong_cake_points = int_ci(root, p + "WrongCakePoints", 0);
        for (int i = 0; i < 11; ++i)
            v.moves_percent[i] =
                int_ci(root, p + "MovesMadeScorePercent" + std::to_string(i * 10), 0);
        v.cakes_needed = int_ci(root, p + "CakesNeeded", 0);
        v.missed_cakes_allowed = int_ci(root, p + "MissedCakesAllowed", 0);
        v.belt_slide_time = float_ci(root, p + "BeltSlideTime", 0.0f);
        v.num_slides_to_add_cake = float_ci(root, p + "NumSlidesToAddCake", 0.0f);
        v.min_layers = int_ci(root, p + "MinLayers", 1);
        v.max_layers = int_ci(root, p + "MaxLayers", 1);
        v.min_toppings = int_ci(root, p + "MinToppings", 1);
        v.max_toppings = int_ci(root, p + "MaxToppings", 1);
        v.num_time_to_advance_belt = float_ci(root, p + "NumTimeToAdvanceBelt", 0.0f);
        v.num_time_to_add_cake = float_ci(root, p + "NumTimeToAddCake", 0.0f);
        v.num_acceleration_to_add_cake_time = float_ci(root, p + "NumAccelerationToAddCakeTime", 0.0f);
        v.num_min_time_to_add_cake = float_ci(root, p + "NumMinTimeToAddCake", 0.0f);
        v.time_attack_allowed = bool_ci(root, p + "TimeAttackAllowed", false);
    }
    vars_ok_ = true;
    return true;
}

bool CakeModel::load_layers(const std::vector<std::uint8_t>& xml, std::string& error) {
    XmlNode root;
    if (!xml_parse(xml, root, error)) return false;
    layers_.clear();
    for (const XmlNode& layer : root.children) {
        if (layer.name != "Layer") continue;
        CakeLayerDef def;
        // A plain layer carries <Delta> and <Sprite>; a shaped layer carries up to three
        // <Shape> entries, one per pan shape.
        const XmlNode* delta = child_ci(layer, "Delta");
        const XmlNode* sprite = child_ci(layer, "Sprite");
        if (delta != nullptr && sprite != nullptr) {
            def.delta = std::atoi(delta->text.c_str());
            def.sprite = sprite->text;
        } else {
            int shapes = 0;
            for (const XmlNode& shape : layer.children) {
                if (shape.name != "Shape") continue;
                const XmlNode* sd = child_ci(shape, "Delta");
                def.delta = sd == nullptr ? 0 : std::atoi(sd->text.c_str());
                const XmlNode* ss = child_ci(shape, "Sprite");
                def.shapes.push_back(ss == nullptr ? std::string{} : ss->text);
                ++shapes;
            }
            if (shapes == 0) continue;  // the shaped form always names at least one
        }
        layers_.push_back(std::move(def));
    }
    if (layers_.empty()) {
        error = "LAYERS.xml carried no layer";
        return false;
    }
    layers_ok_ = true;
    return true;
}

Cake CakeModel::generate_correct(int difficulty, const std::function<int()>& rng) const {
    const CakeVars& v = vars(difficulty);
    const int layer_count = v.min_layers + rng() % (v.max_layers - v.min_layers + 1);
    const int topping_count = v.min_toppings + rng() % (v.max_toppings - v.min_toppings + 1);
    const int shape = rng() % 3;
    Cake cake;
    // Every layer goes through use_tool, which is what Cake::AddLayer does: the engine's
    // generator ignores AddLayer's return value, so a layer the operation matrix refuses never
    // reaches the target.  That is why a generated target is short -- the matrix stops it -- and
    // it is also why the player can always build the target the same way.
    const auto put = [&](int station, int index) {
        CakeLayer added;
        CakeLayer step;
        step.station = station;
        step.index = index;
        cake.moves.push_back(step);
        use_tool(cake, station, index, added, rng);
    };
    for (int i = 0; i < layer_count; ++i) {
        put(1, 3 + shape);      // the pan, which sets the cake's shape
        put(0, rng() % 3);      // the batter
        put(3, 10 + rng() % 3); // the filling
        if (layer_count - i > 1) put(5, 15 + rng() % 3);  // the icing that closes the layer
    }
    put(5, 15 + rng() % 3);  // the icing on the top layer
    // The toppings.  GenerateCorrectCake keeps a list of candidate decorations and takes from it
    // until the target has `topping_count` of them.
    std::vector<int> candidates;
    const bool top_is_decoration =
        !cake.layers.empty() && cake.layers.back().station == 2 &&
        cake.layers.back().index == 5;
    if (top_is_decoration) {
        candidates = {
            6, 8
        };
    } else {
        candidates = {
            8, 6, 9, 7
        };
    }
    if (difficulty < 2) {
        put(5, 15 + rng() % 3);
        if (!candidates.empty())
            put(2, candidates[static_cast<std::size_t>(rng() % candidates.size())]);
    } else {
        bool glaze_used = false;
        if (topping_count != 0) {
            if (rng() % 100 >= 50) {
                if (!candidates.empty()) {
                    const std::size_t pick = static_cast<std::size_t>(rng()) % candidates.size();
                    put(2, candidates[pick]);
                    candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(pick));
                }
            } else {
                put(5, 15 + rng() % 3);
                glaze_used = true;
            }
        }
        for (int k = 1; k < topping_count; ++k) {
            const bool last = k == topping_count - 1;
            const bool draw_deco = !last || (rng() % 100 < 50 && !candidates.empty());
            if (draw_deco) {
                if (!candidates.empty()) {
                    const std::size_t pick = static_cast<std::size_t>(rng()) % candidates.size();
                    put(2, candidates[pick]);
                    candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(pick));
                }
            } else {
                put(4, glaze_used ? (13 + rng() % 2) : 14);
            }
        }
    }
    for (const CakeLayer& layer : cake.layers) cake.height_delta += delta_of(layer.index);
    return cake;
}

CakeUse CakeModel::use_tool(Cake& cake, int station, int index, CakeLayer& added,
                            const std::function<int()>& rng) const {
    added = CakeLayer {};
    added.station = station;
    added.index = index;
    // A pan sets the cake's shape and every later layer inherits it, which is what
    // Cake::AddLayer's replace path does by forwarding the removed layer's shape.
    added.shape = cake.shape;
    if (index >= 3 && index <= 5) {
        added.shape = index - 3;
        cake.shape = index - 3;
    }
    if (station < 0 || station > 5) return CakeUse::Refused;
    // Cake::CanAddLayer (0x10005658C): an empty platter takes a pan and nothing else.
    if (cake.layers.empty() && station != 1) return CakeUse::Refused;
    const int current = cake.layers.empty() ? -1 : layer_class(cake.layers.back().station);
    const int op = cake.layers.empty() ? 6 : kLayerOperation[static_cast<std::size_t>(current)]
                                                       [static_cast<std::size_t>(station)];
    const CakeLayerDef* def = layer(index);
    const int delta = def == nullptr ? 0 : def->delta;
    switch (op) {
        case 0:
            return CakeUse::Refused;
        case 1:
            break;
        case 2:
        case 6:
            // The top layer is replaced.  ComfyCakesGame::GenerateCorrectCake's own sequence
            // starts with a pan, so a first-use pan on an empty platter is the same path.
            if (!cake.layers.empty()) {
                cake.height_delta -= delta_of(cake.layers.back().index);
                cake.layers.pop_back();
            }
            break;
        case 3:
            // Strip back to the last layer that is not a topping.
            while (!cake.layers.empty() &&
                   layer_class(cake.layers.back().station) != 1 &&
                   layer_class(cake.layers.back().station) != 0)
                pop_delta(cake);
            break;
        case 4:
            while (!cake.layers.empty() && cake.layers.back().station == 2) pop_delta(cake);
            break;
        case 5:
            // The final-touch path: a glaze (13) or sugar (14) replaces what is on top.
            if (index != 13 && index != 14) return CakeUse::Refused;
            while (!cake.layers.empty() && cake.layers.back().station == 2) pop_delta(cake);
            break;
        default:
            return CakeUse::Refused;
    }
    cake.layers.push_back(added);
    cake.height_delta += delta;
    return op == 2 ? CakeUse::Replaced : CakeUse::Added;
}

CakeCompare CakeModel::compare(const Cake& a, const Cake& b) const {
    const std::size_t common = std::min(a.layers.size(), b.layers.size());
    // The decorations are compared as a set of four slots, which is why Cake::Compare keeps two
    // four-entry arrays and only fails when one side has a decoration the other lacks.
    int a_deco[4] = {
        -1, -1, -1, -1
    };
    int b_deco[4] = {
        -1, -1, -1, -1
    };
    bool in_deco = false;
    for (std::size_t i = 0; i < common; ++i) {
        const CakeLayer& x = a.layers[i];
        const CakeLayer& y = b.layers[i];
        if (x.station == 2) {
            if (y.station != 2) return CakeCompare::Mismatch;
            const int slot = x.index - 6;
            if (slot < 0 || slot > 3) return CakeCompare::Mismatch;
            a_deco[slot] = x.orientation;
            b_deco[y.index - 6] = y.orientation;
            in_deco = true;
            continue;
        }
        if (in_deco) {
            // The decoration block has ended: compare the accumulated orientations.
            for (int k = 0; k < 4; ++k) {
                if (a_deco[k] == b_deco[k]) continue;
                if (a_deco[k] != -1 && b_deco[k] != -1) return CakeCompare::WrongDecorationKind;
                return CakeCompare::Mismatch;
            }
            in_deco = false;
        }
        if (x.station != y.station || x.index != y.index) return CakeCompare::Mismatch;
        if (x.station == 4) {
            if (x.orientation % 2 != y.orientation % 2) return CakeCompare::WrongOrientation;
        }
        if (x.station == 5 && x.orientation != y.orientation) return CakeCompare::WrongOrientation;
    }
    if (b.layers.size() > a.layers.size()) return CakeCompare::TargetLonger;
    if (b.layers.size() < a.layers.size()) return CakeCompare::SourceLonger;
    return CakeCompare::Exact;
}

}  // namespace pple
