#include "app.hpp"
#include "audio.hpp"
#include <cstdio>

namespace pple::viewer {
void App::cakes_add_paper() {
    if (cakes_over || cakes_belt_present.empty() || cakes_belt_present.front()) return;
    cakes_belt.front() = {};
    cakes_belt.front().station = 0;
    cakes_belt_present.front() = true;
    cakes_transport.positions.front() = {100, -115};
    cakes_transport.inlet_time = 0;
    cakes_clock.added();
    std::printf("cakes: new wax paper on the chute, %zu cake(s) on the belt\n",
        static_cast<std::size_t>(std::count(cakes_belt_present.begin(), cakes_belt_present.end(), true)));
    // CheckEndCondition (0x10005C85F..0x10005C86E): all eight working
    // positions occupied is a clogged line, including the chute, excluding judging.
    if (std::all_of(cakes_belt_present.begin(), cakes_belt_present.begin() + 8,
                    [](bool present) { return present; })) {
        cakes_over = true;
        cakes_won = false;
        play_cake_sfx("CC_LOSTGAME");
        cakes_compute_score();
        cakes_chef_show(6, "GameLoseLineClog");
        hide_tip();
        cakes_chef_pending_tip.clear();
        build_result_panel(false);
        std::printf("cakes: LOST, conveyor line clogged, final score %d\n", cakes_final);
    }
}

bool App::cakes_clock_advance(float dt) {
    if (cakes_over) return false;
    const auto& vars = cakes_model.vars(cakes_difficulty);
    const int old_offset = cakes_clock.indicator_offset(vars);
    // RunTimer (0x10005BE48) stops the round clock while the belt is moving
    // or a tool holds a belt lock. Shipping is independent of that lock.
    const bool paused = cakes_tool_busy || cakes_transport.belt_time >= 0 ||
                        cakes_transport.inlet_time >= 0;
    if (cakes_clock.advance(dt, cakes_difficulty, paused, vars)) {
        std::printf("cakes: automatic conveyor step after %.3f seconds\n", cakes_clock.elapsed);
        cakes_automatic_slide = true;
        cakes_slide_right();
        cakes_automatic_slide = false;
        return true;
    }
    return old_offset != cakes_clock.indicator_offset(vars);
}
}
