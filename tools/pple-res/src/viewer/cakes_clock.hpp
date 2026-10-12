#pragma once
#include <algorithm>
#include "pple/cakes.hpp"

namespace pple::viewer {
// RoundData+56/+60/+78/+79 in normal play. App owns this state; room rebuilds
// never reset it. Time attack is a separate original-game option, not inferred
// from TimeAttackAllowed (which only permits that option).
struct CakeClock {
    float elapsed = 0;
    int slides = 0;
    bool started = false;
    bool entry_clear = false;

    bool advance(float dt, int difficulty, bool paused, const pple::CakeVars& vars) {
        if (!started || difficulty == 0 || paused || vars.belt_slide_time <= 0) return false;
        elapsed += std::max(0.0f, dt);
        return elapsed >= vars.belt_slide_time;
    }
    int indicator_offset(const pple::CakeVars& vars) const {
        const float fraction = vars.belt_slide_time > 0
            ? std::clamp(elapsed / vars.belt_slide_time, 0.0f, 1.0f) : 0;
        return -static_cast<int>(fraction * -32.0f);
    }
    bool add_due(int difficulty, const pple::CakeVars& vars) const {
        return difficulty != 0 && entry_clear && slides + 1 >= vars.num_slides_to_add_cake;
    }
    void slid(bool automatic) {
        elapsed = 0;
        if (!automatic || entry_clear) ++slides;
    }
    void added() { slides = 0; entry_clear = false; }
};
}
