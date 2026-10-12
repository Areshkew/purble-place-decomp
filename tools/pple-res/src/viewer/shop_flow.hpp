#pragma once

#include <algorithm>

namespace pple::viewer {

// Owned by App; reset for each round. Times/frames are from the shipped Curtain
// IntroAnimation.xml (0->7 in .5s, hold 3s, 7->15 in .5s) and OpenAnimation.xml.
// No renderer, asset loading, or wall-clock ownership here.
struct ShopFlow {
    enum class Phase { Intro, Playing, OpeningAnswer, Answer };
    Phase phase = Phase::Intro;
    float elapsed = 0.0f;
    bool counts_visible = false;

    void reset() { *this = ShopFlow{}; }
    void reveal() { phase = Phase::OpeningAnswer; elapsed = 0.0f; }
    void advance(float dt) {
        elapsed += std::max(dt, 0.0f);
        if (phase == Phase::Intro && elapsed >= 4.0f) phase = Phase::Playing;
        if (phase == Phase::OpeningAnswer && elapsed >= 0.5f) phase = Phase::Answer;
    }
    bool mystery_visible() const { return phase == Phase::Intro; }
    bool answer_visible() const { return phase == Phase::OpeningAnswer || phase == Phase::Answer; }
    bool moving() const { return phase == Phase::Intro || phase == Phase::OpeningAnswer; }
    int curtain_frame() const {
        if (phase == Phase::Playing) return 0;
        if (phase == Phase::Answer) return 7;
        if (phase == Phase::OpeningAnswer)
            return std::clamp(static_cast<int>(elapsed * 14.0f), 0, 7);
        if (elapsed < 0.5f) return std::clamp(static_cast<int>(elapsed * 14.0f), 0, 7);
        if (elapsed < 3.5f) return 7;
        return std::clamp(7 + static_cast<int>((elapsed - 3.5f) * 16.0f), 7, 15);
    }
};

}  // namespace pple::viewer
