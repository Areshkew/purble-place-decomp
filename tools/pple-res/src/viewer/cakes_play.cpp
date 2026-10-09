#include "app.hpp"
#include "audio.hpp"
#include "pple/cakes.hpp"

#include <algorithm>
#include <cstdio>

namespace pple::viewer {

// The Comfy Cakes rules, wired to the room cakes_screen.cpp draws.
//
//   a tool button  Tool::UseTool (0x1000751CC) takes the button's <Type> (a LAYERS.xml entry),
//                  finds the cake at the tool's own station on the belt and calls
//                  Cake::CanAddLayer / Cake::CanRemoveAndAddLayer; a refusal plays
//                  GameAudio protos 0x10 and 3 and shakes the tool's machine Base, and raises
//                  the "NeedWaxPaper" tip when there is no cake under it at all
//   the belt       ConveyorBelt::SetCakePosition (0x100063074): slot p is at (98*p - 66, 29)
//                  inside /ConveyorBelt at (0,220); slot 0 is the off-belt slot at (66,-115)
//   the arrows     ComfyCakesGame::SlideLeft (0x10005C8B8) / SlideRight (0x10005C984) clear
//                  RoundData's NumSlides and Timer; ConveyorBelt::SlideLeft (0x1000627F0)
//                  moves every cake one slot towards index 0
//   judging        ComfyCakesGame::OnCakeDone (0x10005C120) compares against the TV's target
//                  and credits RightCakePoints or WrongCakePoints, then CheckEndCondition
//                  (0x10005C7E8)
//   the score      ComfyCakesGame::ComputeScore (0x10005CC4C)
//
// The Chef, the tool and belt animations and the shipped tips are not reproduced here; a click is
// reported and the cakes move, which is what keeps the rules checkable.

namespace {

// Where a cake sits, in belt-container coordinates.  ConveyorBelt::SetCakePosition (0x100063074):
// slot 0 is the off-belt position and slot p >= 1 is at (98*p - 66, 29).
float cakes_slot_x(int slot) {
    return slot <= 0 ? 66.0f : 98.0f * static_cast<float>(slot) - 66.0f;
}
float cakes_slot_y(int slot) {
    return slot <= 0 ? -115.0f : 29.0f;
}

const char* cakes_station_name(int station) {
    switch (station) {
        case 0: return "batter";
        case 1: return "pan";
        case 2: return "decoration";
        case 3: return "filling";
        case 4: return "final touch";
        case 5: return "icing";
        default: return "?";
    }
}

}  // namespace

// The station a tool button belongs to, from the tool document's <Station>.  The buttons are
// named in the tool documents: CakePan*, Batter*, Filling*, Icing*, Decoration*, Final*.
int App::cakes_tool_station(const std::string& button_name) const {
    struct Entry {
        const char* prefix;
        int station;
    };
    static const Entry kEntries[] = {
        {
            "CakePan", 1
        },
        {
            "Batter", 0
        },
        {
            "Decoration", 2
        },
        {
            "Filling", 3
        },
        {
            "Final", 4
        },
        {
            "Icing", 5
        },
    };
    for (const Entry& entry : kEntries)
        if (button_name.rfind(entry.prefix, 0) == 0) return entry.station;
    return -1;
}

// The model's data, loaded once per session.
void App::cakes_load_model() {
    if (cakes_model_tried) return;
    cakes_model_tried = true;
    const auto fetch = [&](const std::string& name, std::vector<std::uint8_t>& out) {
        std::string how;
        const auto* s = bundle.resolve(name, how);
        if (s == nullptr) return false;
        out = bundle.read(*s);
        return true;
    };
    std::vector<std::uint8_t> vars, layers;
    std::string error;
    if (!fetch("COMFYCAKESVARS.XML", vars) || !cakes_model.load_vars(vars, error)) {
        std::fprintf(stderr, "cakes vars: %s\n", error.c_str());
        return;
    }
    if (!fetch("UI\\COMFYCAKES\\LAYERS.XML", layers) ||
        !cakes_model.load_layers(layers, error)) {
        std::fprintf(stderr, "cakes layers: %s\n", error.c_str());
        return;
    }
    std::printf("cakes: %zu layer definition(s) from LAYERS.XML, %d difficulty setting(s) from "
                "COMFYCAKESVARS.XML\n",
                cakes_model.layer_defs().size(), pple::CakeModel::kDifficultyCount);
}

// ComfyCakesGame::NewGame (0x10005B2A8): the round data is zeroed, the target is generated, and
// the first cake is put on the belt.  Tool::UseTool works on the cake at the tool's own station,
// and the first cake goes to ConveyorBelt position 3 on Beginner and 2 otherwise
// (CCTipManager::OnToolButtonClick 0x1000587BC reads that same position back).
void App::cakes_new_game(int difficulty) {
    cakes_load_model();
    if (!cakes_model.layers_ok()) return;
    cakes_difficulty = difficulty;
    cakes_target = cakes_model.generate_correct(difficulty, [] { return std::rand(); });
    cakes_belt.assign(static_cast<std::size_t>(pple::kCakeBeltSlots), pple::Cake {});
    cakes_belt_present.assign(static_cast<std::size_t>(pple::kCakeBeltSlots), false);
    cakes_current_slot = difficulty == 0 ? 3 : 2;
    cakes_belt_present[static_cast<std::size_t>(cakes_current_slot)] = true;
    cakes_belt[static_cast<std::size_t>(cakes_current_slot)].station = cakes_current_slot;
    cakes_correct = 0;
    cakes_missed = 0;
    cakes_moves = 0;
    cakes_slides = 0;
    cakes_score = 0;
    cakes_bonus = 0;
    cakes_final = 0;
    cakes_over = false;
    cakes_won = false;
    cakes_matched_seen = false;
    const pple::CakeVars& v = cakes_model.vars(difficulty);
    std::printf("cakes: new %s game - %d layer(s), %d topping(s), %d cake(s) needed, %d missed "
                "allowed, time attack %s\n",
                difficulty == 0 ? "Beginner"
                                : (difficulty == 1 ? "Intermediate" : "Expert"),
                v.min_layers, v.max_toppings, v.cakes_needed, v.missed_cakes_allowed,
                v.time_attack_allowed ? "on" : "off");
    std::printf("cakes: target cake, %zu layer(s), first cake at belt slot %d\n",
                cakes_target.layers.size(), cakes_current_slot);
    for (const pple::CakeLayer& layer : cakes_target.layers)
        std::printf("  target layer %-12s LAYERS %-2d shape %d\n", cakes_station_name(layer.station),
                    layer.index, layer.shape);
}

// What a tool button does.  Tool::UseTool's refusal path plays two sounds and shakes the
// machine, which the reconstruction reports instead.
void App::cakes_use_tool(int station, int index) {
    if (cakes_over || !cakes_model.layers_ok()) return;
    const std::size_t slot = static_cast<std::size_t>(cakes_current_slot);
    if (slot >= cakes_belt_present.size() || !cakes_belt_present[slot]) {
        std::printf("cakes: station %d, LAYERS %d -> refused, there is no cake under it\n", station,
                    index);
        return;
    }
    pple::CakeLayer added;
    const pple::CakeUse use = cakes_model.use_tool(cakes_belt[slot], station, index, added,
                                                   [] { return std::rand(); });
    const char* answer = "?";
    switch (use) {
        case pple::CakeUse::Refused: answer = "refused"; break;
        case pple::CakeUse::Added: answer = "added"; break;
        case pple::CakeUse::Replaced: answer = "replaced the top layer"; break;
        case pple::CakeUse::Stripped: answer = "stripped the top layers"; break;
    }
    ++cakes_moves;  // RoundData+MovesMade, which Tool::UseTool increments before it decides
    std::printf("cakes: station %d (%s), LAYERS %d -> %s, %d layer(s), %d move(s)\n", station,
                cakes_station_name(station), index, answer,
                static_cast<int>(cakes_belt[slot].layers.size()), cakes_moves);
    if (use == pple::CakeUse::Refused) {
        play_pair_sfx("Con_CakeButtons");
        return;
    }
    play_pair_sfx("Con_CakeBatterButton");
    // ComfyCakesGame::OnToolUsed (0x10005C624) compares after every accepted use and marks the
    // cake wrong the moment it can no longer match the target.
    const pple::CakeCompare cmp = cakes_model.compare(cakes_belt[slot], cakes_target);
    if (cmp != pple::CakeCompare::Exact && cmp != pple::CakeCompare::TargetLonger) {
        if (!cakes_belt[slot].wrong) {
            cakes_belt[slot].wrong = true;
            std::printf("cakes: the cake can no longer match the order\n");
        }
    } else if (cmp == pple::CakeCompare::Exact && !cakes_matched_seen) {
        cakes_matched_seen = true;
        std::printf("cakes: the cake matches the order\n");
    }
}

// ConveyorBelt::SlideLeft (0x1000627F0): every cake moves one slot towards index 0.
// ComfyCakesGame::SlideLeft resets NumSlides and Timer and only runs while the belt is idle.
void App::cakes_slide_left() {
    if (cakes_over) return;
    bool moved = false;
    for (std::size_t p = cakes_belt.size(); p-- > 1;) {
        if (cakes_belt_present[p] && !cakes_belt_present[p - 1]) {
            cakes_belt[p - 1] = cakes_belt[p];
            cakes_belt[p - 1].station = static_cast<int>(p - 1);
            cakes_belt_present[p - 1] = true;
            cakes_belt_present[p] = false;
            moved = true;
        }
    }
    if (!moved) {
        std::printf("cakes: the belt cannot slide left\n");
        return;
    }
    ++cakes_slides;
    std::printf("cakes: the belt slid left (%d slide(s))\n", cakes_slides);
    cakes_judge();
}

// ComfyCakesGame::SlideRight (0x10005C984): every cake moves one slot away from index 0, which
// is the way the player sends a finished cake towards the box at the far end.
void App::cakes_slide_right() {
    if (cakes_over) return;
    bool moved = false;
    for (std::size_t p = 1; p < cakes_belt.size(); ++p) {
        if (cakes_belt_present[p - 1] && !cakes_belt_present[p]) {
            cakes_belt[p] = cakes_belt[p - 1];
            cakes_belt[p].station = static_cast<int>(p);
            cakes_belt_present[p] = true;
            cakes_belt_present[p - 1] = false;
            moved = true;
        }
    }
    if (!moved) {
        std::printf("cakes: the belt cannot slide right\n");
        return;
    }
    ++cakes_slides;
    std::printf("cakes: the belt slid right (%d slide(s))\n", cakes_slides);
}

// ComfyCakesGame::OnCakeDone (0x10005C120) and CheckEndCondition (0x10005C7E8).  A cake that
// reaches the end of the line is compared against the target: an exact match credits
// RightCakePoints, anything else credits WrongCakePoints, which COMFYCAKESVARS.XML carries as a
// negative number.
void App::cakes_judge() {
    if (cakes_belt_present.empty() || !cakes_belt_present.front()) return;
    const pple::CakeVars& v = cakes_model.vars(cakes_difficulty);
    const pple::CakeCompare cmp = cakes_model.compare(cakes_belt.front(), cakes_target);
    const bool right = cmp == pple::CakeCompare::Exact;
    if (right) {
        ++cakes_correct;
        cakes_score += v.right_cake_points;
        std::printf("cakes: cake judged correct (%d/%d), +%d point(s)\n", cakes_correct,
                    v.cakes_needed, v.right_cake_points);
    } else {
        ++cakes_missed;
        cakes_score += v.wrong_cake_points;
        std::printf("cakes: cake judged wrong (%d), %+d point(s)\n", cakes_missed,
                    v.wrong_cake_points);
    }
    cakes_belt_present.front() = false;
    cakes_belt.front() = pple::Cake {};
    // ConveyorBelt::AddCake brings a new cake down the chute while the round is still running.
    if (cakes_correct < v.cakes_needed) {
        const std::size_t last = cakes_belt.size() - 1;
        if (!cakes_belt_present[last]) {
            cakes_belt_present[last] = true;
            cakes_belt[last] = pple::Cake {};
            cakes_belt[last].station = static_cast<int>(last);
        }
    }
    if (cakes_correct >= v.cakes_needed) {
        cakes_over = true;
        cakes_won = true;
        cakes_compute_score();
        std::printf("cakes: WON after %d cake(s), final score %d\n", cakes_correct, cakes_final);
    } else if (cakes_difficulty != 0 && cakes_missed >= v.missed_cakes_allowed) {
        cakes_over = true;
        cakes_won = false;
        cakes_compute_score();
        std::printf("cakes: LOST after %d mistake(s), final score %d\n", cakes_missed,
                    cakes_final);
    }
}

// ComfyCakesGame::ComputeScore (0x10005CC4C).  The moves bonus comes from the difficulty's
// eleven <MovesMadeScorePercent*> bands, chosen by how far MovesMade overshot the minimum, which
// is one cake's layer count times CakesNeeded.
void App::cakes_compute_score() {
    const pple::CakeVars& v = cakes_model.vars(cakes_difficulty);
    int per_cake = 0;
    for (const pple::CakeLayer& layer : cakes_target.layers) {
        // ComputeScore counts a layer once, and the layers it does not count once twice.
        per_cake += (layer.station != 0 && layer.index != 13) ? 1 : 2;
    }
    const int minimum = per_cake * v.cakes_needed;
    int band = 0;
    if (cakes_moves > minimum) {
        const int over = (cakes_moves - minimum) * 10;
        band = minimum <= 0 ? 10 : std::min(10, over / minimum);
    }
    cakes_bonus = v.moves_percent[static_cast<std::size_t>(band)];
    cakes_final = cakes_score + cakes_bonus;
    std::printf("cakes: %d move(s) of a %d-move minimum, band %d%%, bonus %d, final score %d\n",
                cakes_moves, minimum, band * 10, cakes_bonus, cakes_final);
}

// Draw a cake's layers bottom-up, each shifted by the running sum of the earlier layers' <Delta>.
// CakeScreen::SetCake anchors the TV's cake at CakeX/CakeY (65,104).
void App::cakes_draw_cake(const pple::Cake& cake, float base_x, float base_y) {
    if (!cakes_model.layers_ok()) return;
    float y = base_y;
    for (const pple::CakeLayer& layer : cake.layers) {
        const pple::CakeLayerDef* def = cakes_model.layer(layer.index);
        if (def == nullptr) continue;
        const std::string& sprite = def->shapes.empty()
                                        ? def->sprite
                                        : def->shapes[static_cast<std::size_t>(std::clamp(
                                            layer.shape < 0 ? 0 : layer.shape, 0,
                                            static_cast<int>(def->shapes.size()) - 1))];
        if (sprite.empty()) {
            std::fprintf(stderr, "cakes layer %d: no sprite for shape %d\n", layer.index,
                         layer.shape);
            continue;
        }
        const viewer::Sprite sp = resolve_sprite(bundle, sprite, 0);
        if (!sp.ok) {
            std::fprintf(stderr, "cakes layer %d: %s\n", layer.index, sp.note.c_str());
            continue;
        }
        std::string how;
        const auto* slot = bundle.resolve(sp.requested, how);
        if (slot == nullptr) continue;
        std::string note;
        SDL_Texture* tex = load_sheet_texture(sp.requested, slot->entry->name, note);
        if (tex == nullptr) continue;
        pple::SceneItem geometry;
        geometry.src_x = sp.src.x;
        geometry.src_y = sp.src.y;
        geometry.src_w = sp.src.w;
        geometry.src_h = sp.src.h;
        geometry.frames = sp.frames;
        geometry.per_row = sp.per_row;
        geometry.prepped = sp.prepped;
        float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
        pple::sprite_frame_rect(geometry, 0, fx, fy, fw, fh);
        DrawItem d;
        d.texture = tex;
        d.src = SDL_FRect{fx, fy, fw, fh};
        d.dst = SDL_FRect{base_x - sp.reg_x, y - sp.reg_y, fw, fh};
        scene_draws.push_back(d);
        cakes_draw_names.push_back(sprite);
        y += static_cast<float>(def->delta);
    }
}

// One frame of the room with the cakes on it: the static layer first, then the TV's target and
// every cake on the belt.
void App::cakes_refresh() {
    if (!cakes_ready) return;
    build_cakes();
    cakes_draw_cake(cakes_target, 65.0f, 104.0f);
    for (std::size_t p = 0; p < cakes_belt.size(); ++p) {
        if (p >= cakes_belt_present.size() || !cakes_belt_present[p]) continue;
        cakes_draw_cake(cakes_belt[p], cakes_slot_x(static_cast<int>(p)),
                        220.0f + cakes_slot_y(static_cast<int>(p)));
    }
    std::size_t belt_cakes = 0;
    for (bool present : cakes_belt_present)
        if (present) ++belt_cakes;
    hud_lines = {cakes_xml,
                 "target " + std::to_string(cakes_target.layers.size()) + " layer(s), " +
                     std::to_string(belt_cakes) + " cake(s) on the belt",
                 "score " + std::to_string(cakes_score) + "  moves " +
                     std::to_string(cakes_moves) + "  correct " + std::to_string(cakes_correct) +
                     "/" + std::to_string(cakes_model.vars(cakes_difficulty).cakes_needed),
                 cakes_over ? (cakes_won ? std::string("WON") : std::string("LOST"))
                            : std::string("click a tool button, or a belt arrow")};
    rebuild_hud();
    cakes_dump_draws();
}

}  // namespace pple::viewer
