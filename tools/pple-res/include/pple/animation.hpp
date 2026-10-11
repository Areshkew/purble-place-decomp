#pragma once
#include "pple/xml.hpp"
#include <vector>

namespace pple {
// ResourceAnimation: blocks run together until WaitForComplete. Position has two Types,
// one per axis. IntMessage values are property crossings, not events at block completion.
struct AnimationRun {
    enum class Affects { Frame, X, Y, Alpha };
    Affects affects = Affects::Frame;
    float from = 0, to = 0, seconds = 0, start = 0;
    bool relative = false;
};
struct AnimationCue { float time = 0; int value = 0; bool complete = false; };
struct AnimationSet {
    std::vector<AnimationRun> runs;
    std::vector<AnimationCue> cues;
    float total = 0;
};
struct AnimationValue { float frame = 0, dx = 0, dy = 0, alpha = 255; };
bool animation_parse(const std::vector<unsigned char>& bytes, AnimationSet& out);
AnimationValue animation_value(const AnimationSet& set, float time, bool reverse = false);
float animation_cue_time(const AnimationSet& set, int value, float fallback);
// The original pan/decoration timers integrate integer displacement at 60 Hz.
struct AnimationDrop { float y = 0, scale = 1; float seconds = 0; };
AnimationDrop animation_drop(float from, float to, float time, float velocity, float acceleration,
                             bool grow = false);
}
