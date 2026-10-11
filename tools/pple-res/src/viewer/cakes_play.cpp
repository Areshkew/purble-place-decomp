#include "app.hpp"
#include "audio.hpp"
#include "pple/cakes.hpp"

#include <algorithm>
#include <cstdio>

namespace pple::viewer {

// The Comfy Cakes rules, wired to the room cakes_screen.cpp draws.
//
//   a tool button  Tool::UseTool (0x1000751CC) takes the button's <Type> (a LAYERS.xml entry),
//                  finds the cake at the tool's own belt position and calls
//                  Cake::CanAddLayer / Cake::CanRemoveAndAddLayer; a refusal plays two GameAudio
//                  protos and shakes the machine Base with MachineUnusableShakeAnim.xml, and
//                  raises the "NeedWaxPaper" tip when there is no cake in reach at all
//   the machines  each <Tool> runs its own animation for the button's flavour and the layer is
//                  applied when it completes: Tool::FinishAddTool (0x10007548A) ->
//                  Cake::AddLayer, and the machine's falling stream and fill sprite carry the
//                  ingredient onto the cake while it plays
//   the belt       ConveyorBelt::SetCakePosition (0x100063074): slot p is at (98*p - 66, 29)
//                  inside /ConveyorBelt at (0,220); slot 0 is the chute the replacement cake
//                  arrives on, and the last slot (8) is where a cake is judged.  Sliding left
//                  ascends and right descends, so each cake moves exactly one slot per slide
//   the arrows     ComfyCakesGame::SlideLeft (0x10005C8B8) / SlideRight (0x10005C984) clear
//                  RoundData's NumSlides and Timer and first call Tool::CompleteTool on every
//                  machine, so a click on an arrow lands the layer a machine was carrying
//   judging       ConveyorBelt::SlideRight harvests the cake that reaches the last slot
//                  (0x100062D64) and ComfyCakesGame::OnCakeDone (0x10005C120) compares it
//                  against the TV's target, credits RightCakePoints or WrongCakePoints, then
//                  CheckEndCondition (0x10005C7E8)
//   the score      ComfyCakesGame::ComputeScore (0x10005CC4C)

namespace {

// Where a cake sits, in belt-container coordinates.  ConveyorBelt::SetCakePosition (0x100063074):
// slot 0 is the chute and slot p >= 1 is at (98*p - 66, 29).  ConveyorBelt::AddCake
// (0x100062F9E) puts the replacement on the chute at (100,-115); the engine's own chute value is
// the off-screen position either way, and the room clips it exactly as the Pairs board does.

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
    initialise_cake_audio(bundle, mute);
    cakes_tools_load();
    cakes_tools_stop();
    cakes_transport.motion.clear();
    cakes_transport.belt_time = cakes_transport.shipping_time = -1;
    cakes_transport.judge_after_slide = false;
    cakes_transport.readout_time = -1;
    cakes_transport.readout_slot = -1;
    cakes_transport.shipping_cake = {};
    cakes_transport.shipping_pending.clear();
    cakes_minimum_moves = 0;
    cakes_tool_shake = -1.0f;
    std::fill(std::begin(cakes_machine_type), std::end(cakes_machine_type), 0);
    cakes_difficulty = difficulty;
    cakes_target = cakes_model.generate_correct(difficulty, [] { return std::rand(); });
    play_cake_sfx("CC_CAKEONTV");
    cakes_belt.assign(static_cast<std::size_t>(pple::kCakeBeltSlots), pple::Cake {});
    cakes_belt_present.assign(static_cast<std::size_t>(pple::kCakeBeltSlots), false);
    cakes_transport.positions.resize(cakes_belt.size());
    for (std::size_t p = 0; p < cakes_belt.size(); ++p)
        pple::CakeModel::belt_position(static_cast<int>(p), cakes_transport.positions[p].first,
            cakes_transport.positions[p].second);
    // ComfyCakesGame::NewGame's AddInitialCake (0x10005B4CE): RoundData+12 picks the belt
    // position -- 0 for a Beginner round and non-zero otherwise -- and the cake is put straight
    // there, at slot 3 or slot 2, without going through the chute.
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
    // NewGame (0x10005B564) arms a one-second timer and CCTipManager::ChooseStartGameTip picks
    // the opening bubble; the Chef himself walks in with it.
    cakes_load_chef();
    cakes_chef_show(0, "");
    if (difficulty == 0) cakes_chef_show(4, "GameStartFirstA");
    else cakes_chef_show(4, "GameStartFirstB");
    const pple::CakeVars& v = cakes_model.vars(difficulty);
    std::printf("cakes: new %s game - %d layer(s), %d topping(s), %d cake(s) needed, %d missed "
                "allowed, time attack %s\n",
                difficulty == 0 ? "Beginner"
                                : (difficulty == 1 ? "Intermediate" : "Expert"),
                v.min_layers, v.max_toppings, v.cakes_needed, v.missed_cakes_allowed,
                v.time_attack_allowed ? "on" : "off");
    // ComfyCakesGame::NewGame's NewGame(false) branch queues the "Start Here" tip behind the
    // opening bubble, which is what first teaches the player to lay down a pan.
    if (difficulty == 0) cakes_chef_show(4, "GameStartFirstA");
    std::printf("cakes: target cake, %zu layer(s), first cake at belt slot %d\n",
                cakes_target.layers.size(), cakes_current_slot);
    for (const pple::CakeLayer& layer : cakes_target.layers)
        std::printf("  target layer %-12s LAYERS %-2d shape %d\n", cakes_station_name(layer.station),
                    layer.index, layer.shape);
}

// Which belt slot holds the cake being built.  The reconstruction holds one cake at a time: a
// replacement appears on the chute (slot 0) after a judged cake, and the next slide brings it
// into the line, so the occupied slot is the one that matters.
int App::cakes_working_slot() const {
    for (std::size_t p = cakes_belt.size(); p-- > 0;)
        if (cakes_belt_present[p]) return static_cast<int>(p);
    return -1;
}

// What a tool button does.  Tool::UseTool takes the cake at the tool's own belt position --
// InitGameUI (0x10005DABC) gives CakePanTool slot 2, BatterTool 3, FillingTool 4, IcingTool 5,
// DecorationTool 6 and FinalTouchTool 7 -- so a cake has to be slid under its machine first.  The
// refusal branch plays two sounds, shakes the machine with MachineUnusableShakeAnim.xml and, when
// no cake is in reach at all, raises the "NeedWaxPaper" bubble.
void App::cakes_use_tool(int station, int index) {
    cakes_transport_finish_slide();
    if (cakes_over || !cakes_model.layers_ok() || !cakes_tool_visible(station)) return;
    // A machine that is still playing its last animation lands it first, exactly as an arrow
    // click does (ComfyCakesGame::SlideLeft calls Tool::CompleteTool on every machine), so a
    // fast click never loses a layer.
    cakes_tool_complete();
    ++cakes_moves;  // RoundData+MovesMade, which Tool::UseTool increments before it decides
    const int slot = cakes_tool_slot_for(station);
    const bool reachable = slot >= 0 && static_cast<std::size_t>(slot) < cakes_belt_present.size() &&
                           cakes_belt_present[static_cast<std::size_t>(slot)];
    const char* answer = nullptr;
    if (reachable) {
        cakes_tool_before = cakes_belt[static_cast<std::size_t>(slot)];
        pple::CakeLayer added;
        const pple::CakeUse use =
            cakes_model.use_tool(cakes_belt[static_cast<std::size_t>(slot)], station, index, added,
                                 [] { return std::rand(); });
        cakes_tool_replacing = use == pple::CakeUse::Replaced || use == pple::CakeUse::Stripped;
        switch (use) {
            case pple::CakeUse::Added: answer = "added"; break;
            case pple::CakeUse::Replaced: answer = "replaced the top layer"; break;
            case pple::CakeUse::Stripped: answer = "stripped the top layers"; break;
            case pple::CakeUse::Refused: break;
        }
    }
    if (answer == nullptr) {
        // Tool::UseTool's else branch (0x10007539B).
        play_cake_sfx("CC_TOOLBUTTONS");
        play_cake_sfx("BAD_MOVE");
        cakes_tools_shake(station);
        std::printf("cakes: station %d (%s), LAYERS %d -> refused, %s, %d move(s)\n", station,
                    cakes_station_name(station), index,
                    reachable ? "the cake cannot take it" : "there is no cake under it", cakes_moves);
        return;
    }
    std::printf("cakes: station %d (%s), LAYERS %d -> %s, %d layer(s), %d move(s)\n", station,
                cakes_station_name(station), index, answer,
                static_cast<int>(cakes_belt[static_cast<std::size_t>(slot)].layers.size()),
                cakes_moves);
    // Use the station's own sound, including the two Advanced final touches.
    const char* event = nullptr;
    switch (station) {
        case 0: event = "CC_CAKEBATTERBUTTON"; break;
        case 1: event = "CC_CAKEPANBUTTON"; break;
        case 2: event = "CC_DECORATIONBUTTON"; break;
        case 3: event = "CC_CAKEFILLINGBUTTON"; break;
        case 4: event = index == 13 ? "CC_FLAME" : "CC_SUGAR"; break;
        case 5: event = "CC_FROSTINGBUTTON"; break;
    }
    if (event) play_cake_sfx(event);
    // The machine plays its own animation and the layer is shown again when it ends: the engine
    // hides the cake's top layer while the machine's fill sprite grows the ingredient in
    // (PlayAddToolAnimation 0x100075F46) and FinishAddTool (0x10007548A) applies the layer.
    cakes_tools_start(station, index,
                      cakes_belt[static_cast<std::size_t>(slot)].shape, slot);
}

// ComfyCakesGame::OnToolUsed (0x10005C624), run when the machine's animation completes: the
// cake is compared with the TV's order and marked wrong the moment it can no longer match.
void App::cakes_tool_complete() {
    if (!cakes_tool_busy) return;
    const int slot = cakes_tool_slot_running;
    const int station = cakes_tool_station_running;
    cakes_tools_stop();
    if (slot < 0 || static_cast<std::size_t>(slot) >= cakes_belt.size()) return;
    const pple::CakeCompare cmp = cakes_model.compare(cakes_belt[static_cast<std::size_t>(slot)],
                                                     cakes_target);
    if (cmp != pple::CakeCompare::Exact && cmp != pple::CakeCompare::TargetLonger) {
        if (!cakes_belt[static_cast<std::size_t>(slot)].wrong) {
            cakes_belt[static_cast<std::size_t>(slot)].wrong = true;
            std::printf("cakes: the cake can no longer match the order\n");
        }
    } else if (cmp == pple::CakeCompare::Exact && !cakes_matched_seen) {
        cakes_matched_seen = true;
        if (cakes_difficulty != 2) cakes_chef_show(4, "CakeMatchedOrder");
        std::printf("cakes: the cake matches the order\n");
    }
    std::printf("cakes: station %d (%s) finished\n", station, cakes_station_name(station));
    cakes_refresh();
}

// ComfyCakesGame::SlideLeft (0x10005C8B8): every cake moves one slot towards index 0, and the
// round's slide counter and timer are cleared.  It first calls Tool::CompleteTool on every
// machine, so a click on an arrow lands whatever layer a machine was still carrying.
void App::cakes_slide_left() {
    cakes_transport_finish_slide();
    if (cakes_over) return;
    cakes_tool_complete();
    bool moved = false;
    // Ascending, exactly as ConveyorBelt::SlideLeft's `for (v4 = 1; v4 < v3; ++v4)` loop does:
    // slot p-1 is filled only after it has already been tested, so each cake moves exactly one
    // slot per slide instead of cascading to the end of the line.
    for (std::size_t p = 1; p < cakes_belt.size(); ++p) {
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
    play_cake_sfx("LEFT_BUTTON");
    play_cake_sfx("CC_CONVEYOR");
    ++cakes_slides;
    std::printf("cakes: the belt slid left (%d slide(s))\n", cakes_slides);
    cakes_transport_slide(-1);
}

// ComfyCakesGame::SlideRight (0x10005C984): every cake moves one slot away from index 0, which
// is the way the player sends a finished cake down the line towards the shipping end.  A cake
// that reaches the last slot is judged: ConveyorBelt::SlideRight harvests it (0x100062D64) and
// OnCakeDone compares it against the TV's order.
void App::cakes_slide_right() {
    cakes_transport_finish_slide();
    if (cakes_over) return;
    cakes_tool_complete();
    bool moved = false;
    // Descending, the mirror of the left slide: slot p is filled only after it has been tested,
    // so each cake moves exactly one slot per slide.
    for (std::size_t p = cakes_belt.size(); p-- > 1;) {
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
    play_cake_sfx("RIGHT_BUTTON");
    play_cake_sfx("CC_CONVEYOR");
    ++cakes_slides;
    std::printf("cakes: the belt slid right (%d slide(s))\n", cakes_slides);
    cakes_transport_slide(1);
}

// Slide the belt until the cake being built sits under `want`.  A cake that reaches the last
// slot on the way is judged, which takes it off the line, so the walk stops there.
void App::cakes_slide_to(int want) {
    const int judged = cakes_correct + cakes_missed;
    for (int guard = 0; guard < 16 && !cakes_over; ++guard) {
        const int at = cakes_working_slot();
        if (at < 0 || at == want) return;
        if (cakes_correct + cakes_missed != judged) return;
        if (at < want) cakes_slide_right();
        else cakes_slide_left();
    }
}

// Play a whole round out: build the current target through the same click path the mouse uses,
// then slide it down the line to be judged.  --cakes-solve runs this, the way --pairs-solve does
// for Purble Pairs, and it is the hook that verifies the whole loop headlessly.
void App::cakes_solve() {
    if (!cakes_model.layers_ok()) return;
    const pple::CakeVars& v = cakes_model.vars(cakes_difficulty);
    const int last = static_cast<int>(cakes_belt.size()) - 1;
    std::printf("cakes solve: %s, %d cake(s) needed\n",
                cakes_difficulty == 0 ? "Beginner"
                                      : (cakes_difficulty == 1 ? "Intermediate" : "Expert"),
                v.cakes_needed);
    int guard = 0;
    while (!cakes_over && guard++ < 400) {
        // Replay the generator's own tool uses.  That is the sequence a player has to follow, and
        // it includes the pans that Cake::AddLayer's replace operation removes again.  Each use
        // needs the cake under that step's own machine, so the belt is slid to it first.
        const auto steps = cakes_target.moves;
        for (const pple::CakeLayer& step : steps) {
            cakes_slide_to(cakes_tool_slot_for(step.station));
            if (cakes_over) break;
            cakes_use_tool(step.station, step.index);
            if (cakes_over) break;
        }
        // Ship the finished cake: slide it to the last slot, where ConveyorBelt::SlideRight
        // harvests it and OnCakeDone compares it against the order.
        const int judged = cakes_correct + cakes_missed;
        for (int slide = 0; slide < 16 && !cakes_over; ++slide) {
            if (cakes_correct + cakes_missed != judged) break;
            const int at = cakes_working_slot();
            if (at < 0 || at >= last) break;
            cakes_slide_right();
            cakes_transport_finish_slide();
        }
    }
    std::printf("cakes solve: finished, %s, score %d, %d correct, %d missed, %d move(s)\n",
                cakes_won ? "WON" : "LOST", cakes_score, cakes_correct, cakes_missed, cakes_moves);
}

// ComfyCakesGame::OnCakeDone (0x10005C120) and CheckEndCondition (0x10005C7E8).  A cake that
// reaches the last slot of the belt is compared against the target: an exact match credits
// RightCakePoints, anything else credits WrongCakePoints, which COMFYCAKESVARS.XML carries as a
// negative number.
void App::cakes_judge() {
    if (cakes_belt_present.empty() || !cakes_belt_present.back()) return;
    const pple::CakeVars& v = cakes_model.vars(cakes_difficulty);
    const pple::CakeCompare cmp = cakes_model.compare(cakes_belt.back(), cakes_target);
    const bool right = cmp == pple::CakeCompare::Exact;
    play_cake_sfx(right ? "CC_CORRECTCAKE" : "CC_BADCAKE");
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
    if (right) {
        cakes_transport.readout_time = -1; // a success stops the next slot's rejection flash
    } else {
        cakes_transport.readout_slot = cakes_correct;
        cakes_transport.readout_time = 0;
    }
    if (right) {
        for (const auto& layer : cakes_target.layers)
            cakes_minimum_moves += (layer.station != 0 && layer.index != 13) ? 1 : 2;
    }
    cakes_transport_ship(cakes_belt.back(), right, cakes_transport.positions.back().first,
        220 + cakes_transport.positions.back().second);
    cakes_belt_present.back() = false;
    cakes_belt.back() = pple::Cake {};
    // ConveyorBelt::AddCake (0x100062E44) brings a replacement down the chute onto slot 0 while
    // the round is still running; the next right slide carries it into the line at slot 1.
    if (cakes_correct < v.cakes_needed && (cakes_difficulty == 0 || cakes_missed < v.missed_cakes_allowed)) {
        if (right) cakes_next_target(); // wrong cakes retry the existing order
        cakes_belt_present.front() = true;
        cakes_belt.front() = pple::Cake {};
        cakes_belt.front().station = 0;
        cakes_transport.positions.front() = {100, -115};
        cakes_transport.motion = {{0, 100, -115, cakes_transport.animations["CakeSlideDownLeft"]}};
        cakes_transport.belt_time = 0;
        cakes_transport.belt_total = cakes_transport.animations["CakeSlideDownLeft"].total;
    }
    if (cakes_correct >= v.cakes_needed) {
        cakes_over = true;
        cakes_won = true;
        play_cake_sfx("CC_WONGAME");
        cakes_compute_score();
        // ComfyCakesGame::OnEnterState(2), the win: a random GameWin* bubble and state 2 of the
        // Chef's own machine.
        cakes_chef_show(4, "GameWinNextA1");
        std::printf("cakes: WON after %d cake(s), final score %d\n", cakes_correct, cakes_final);
    } else if (cakes_difficulty != 0 && cakes_missed >= v.missed_cakes_allowed) {
        cakes_over = true;
        cakes_won = false;
        play_cake_sfx("CC_LOSTGAME");
        cakes_compute_score();
        // CheckEndCondition's GameLoseTooMany: the Chef's lose movie and state 6.
        cakes_chef_show(6, "GameLoseTooMany");
        std::printf("cakes: LOST after %d mistake(s), final score %d\n", cakes_missed,
                    cakes_final);
    }
    if (cakes_over) {
        hide_tip();
        cakes_chef_pending_tip.clear();
        build_result_panel(cakes_won);
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
    const int minimum = cakes_minimum_moves + per_cake * std::max(0, v.cakes_needed - cakes_correct);
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

// Where a cake's top layer sits, inside the belt container.  Cake::PutLayerOnTop (0x10005733C)
// puts a layer's node at the cake's running <Delta> sum and then adds the layer's own <Delta>,
// which is the same walk the drawing below makes.
float App::cakes_cake_top_y(const pple::Cake& cake, int hide_top) const {
    float y = 0.0f;
    const std::size_t visible = cake.layers.size() >
                                        static_cast<std::size_t>(std::max(0, hide_top))
                                    ? cake.layers.size() - static_cast<std::size_t>(std::max(0, hide_top))
                                    : 0;
    for (std::size_t i = 0; i < visible; ++i) {
        const pple::CakeLayerDef* def = cakes_model.layer(cake.layers[i].index);
        y += static_cast<float>(def == nullptr ? 0 : def->delta_for(cake.layers[i].shape));
    }
    return y;
}

// Draw a cake's layers bottom-up, each shifted by the running sum of the earlier layers' <Delta>.
// CakeScreen::SetCake anchors the TV's cake at CakeX/CakeY (65,104), and a belt cake at its own
// ConveyorBelt::SetCakePosition slot.  `hide_top` leaves the newest layer out, which is what a
// machine's animation does while its fill sprite grows the ingredient in.
void App::cakes_draw_cake(const pple::Cake& cake, float base_x, float base_y, int hide_top, float scale) {
    if (!cakes_model.layers_ok()) return;
    float y = base_y;
    const std::size_t visible = cake.layers.size() >
                                        static_cast<std::size_t>(std::max(0, hide_top))
                                    ? cake.layers.size() - static_cast<std::size_t>(std::max(0, hide_top))
                                    : 0;
    for (std::size_t i = 0; i < visible; ++i) {
        const pple::CakeLayer& layer = cake.layers[i];
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
        // CakeScreen::SetCake truncates the scaled local layer position before rendering.
        const float layer_y = static_cast<float>(static_cast<int>((y - base_y) * scale));
        d.dst = SDL_FRect{base_x - sp.reg_x * scale, base_y + layer_y - sp.reg_y * scale,
                          fw * scale, fh * scale};
        scene_draws.push_back(d);
        cakes_draw_names.push_back(sprite);
        y += static_cast<float>(def->delta_for(layer.shape));
    }
}

// One frame of the room with the cakes on it: the static room first, then the Chef, the TV's
// target, every cake on the belt, and finally the machines of the tool animation in flight.
// build_cakes is the expensive half (the scene load and its sheet textures), so it is only run by
// cakes_refresh; this is the per-frame half and it rewrites everything after the static room, so
// a machine's animation and the Chef's movie play without rebuilding the room.
void App::cakes_redraw() {
    if (!cakes_ready) return;
    scene_draws.resize(cakes_room_at);
    cakes_draw_names.resize(cakes_room_at);
    cakes_transport_draw(false);
    cakes_draw_chef();
    // CakeScreen::SetCake (0x100057C78) scales each layer to 80% and offsets the
    // container by +3 per batter/filling, or -1 for a heart-shaped layer (shape type 5).
    int target_y = 104;
    for (const pple::CakeLayer& layer : cakes_target.layers)
        if (layer.station == 0 || layer.station == 3) target_y += layer.shape == 2 ? -1 : 3;
    cakes_draw_cake(cakes_target, 65.0f, static_cast<float>(target_y), 0, 0.8f);
    for (std::size_t p = 0; p < cakes_belt.size(); ++p) {
        if (p >= cakes_belt_present.size() || !cakes_belt_present[p]) continue;
        // Cake::Init (0x100055E9C) creates visible wax paper even before a pan is added.
        pple::SceneItem paper;
        paper.kind = pple::NodeKind::Sprite;
        paper.instance = "WaxPaper";
        paper.ref = "UI\\ComfyCakes\\WaxPaperSprite.xml";
        float x, y;
        cakes_transport_position(static_cast<int>(p), x, y);
        paper.x = x; paper.y = 220 + y;
        cakes_append(paper);
        const bool active = cakes_tool_busy && cakes_tool_slot_running == static_cast<int>(p);
        if (!active) cakes_draw_cake(cakes_belt[p], x, 220 + y);
        else if (cakes_tool_station_running == 0)
            cakes_draw_cake(cakes_tool_before, x, 220 + y, cakes_tool_replacing ? 1 : 0);
        else if (cakes_tool_station_running == 4 && cakes_tool_time >= cakes_tool_reveal_at)
            cakes_draw_cake(cakes_belt[p], x, 220 + y);
        else cakes_draw_cake(cakes_tool_before, x, 220 + y);
    }
    cakes_tools_draw();
    cakes_transport_draw(true);
    std::size_t belt_cakes = 0;
    for (bool present : cakes_belt_present)
        if (present) ++belt_cakes;
    const std::vector<std::string> next_hud = {cakes_xml,
                 "target " + std::to_string(cakes_target.layers.size()) + " layer(s), " +
                     std::to_string(belt_cakes) + " cake(s) on the belt",
                 "score " + std::to_string(cakes_score) + "  moves " +
                     std::to_string(cakes_moves) + "  correct " + std::to_string(cakes_correct) +
                     "/" + std::to_string(cakes_model.vars(cakes_difficulty).cakes_needed),
                 cakes_over ? (cakes_won ? std::string("WON - N for a new round") : std::string("LOST - N for a new round"))
                            : std::string("click tools / belt arrows; 1/2/3 difficulty; N new round")};
    if (next_hud != hud_lines) {
        hud_lines = next_hud;
        rebuild_hud();
    }
}

void App::cakes_refresh() {
    if (!cakes_ready) return;
    build_cakes();
    cakes_room_at = scene_draws.size();
    cakes_redraw();
    cakes_dump_draws();
}

}  // namespace pple::viewer
