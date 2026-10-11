#include "pple/animation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace pple {
namespace {
float number(const XmlNode& n, const char* name, float fallback = 0) {
    const auto* c = n.child(name);
    return c ? static_cast<float>(std::atof(c->text.c_str())) : fallback;
}
}
bool animation_parse(const std::vector<unsigned char>& bytes, AnimationSet& out) {
    out = {};
    XmlNode root;
    std::string error;
    if (!xml_parse(bytes, root, error)) return false;
    float group_start = 0, group_end = 0;
    for (const auto& block : root.children) {
        if (block.name == "WaitForComplete") { group_start = group_end; continue; }
        if (block.name != "Animation") continue;
        const auto* affects = block.child("Affects");
        if (!affects) continue;
        int axis = 0;
        const std::size_t first = out.runs.size();
        float end = group_start;
        for (const auto& type : block.children) {
            if (type.name != "Type") continue;
            AnimationRun run;
            if (affects->text == "CurrentFrame") run.affects = AnimationRun::Affects::Frame;
            else if (affects->text == "Alpha") run.affects = AnimationRun::Affects::Alpha;
            else if (affects->text == "XPosition") run.affects = AnimationRun::Affects::X;
            else if (affects->text == "YPosition") run.affects = AnimationRun::Affects::Y;
            else if (affects->text == "Position") run.affects = axis++ == 0 ? AnimationRun::Affects::X : AnimationRun::Affects::Y;
            else continue;
            run.start = group_start;
            run.seconds = std::max(0.0f, number(type, "Time"));
            run.relative = type.attr("Name") == "LinearRelative";
            run.from = run.relative ? 0 : number(type, "Start");
            run.to = number(type, run.relative ? "Count" : "End");
            end = std::max(end, run.start + run.seconds);
            out.runs.push_back(run);
        }
        for (const auto& cue : block.children) {
            if (cue.name != "IntMessage" && cue.name != "SendComplete") continue;
            const int value = std::atoi(cue.text.c_str());
            float time = end;
            if (cue.name == "IntMessage" && out.runs.size() > first) {
                const auto& run = out.runs[first];
                const float range = run.to - run.from;
                time = run.start + (range == 0 ? 0 : std::clamp((value - run.from) / range, 0.0f, 1.0f) * run.seconds);
            }
            out.cues.push_back({time, value, cue.name == "SendComplete"});
        }
        group_end = std::max(group_end, end);
    }
    out.total = group_end;
    std::stable_sort(out.cues.begin(), out.cues.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return !out.runs.empty();
}
AnimationValue animation_value(const AnimationSet& set, float time, bool reverse) {
    AnimationValue v;
    if (reverse) time = set.total - time;
    for (const auto& run : set.runs) {
        if (time < run.start) continue;
        const float k = run.seconds > 0 ? std::clamp((time - run.start) / run.seconds, 0.0f, 1.0f) : 1;
        const float value = run.from + (run.to - run.from) * k;
        switch (run.affects) {
            case AnimationRun::Affects::Frame: v.frame = value; break;
            case AnimationRun::Affects::Alpha: v.alpha = value; break;
            case AnimationRun::Affects::X: v.dx = run.relative ? v.dx + value : value; break;
            case AnimationRun::Affects::Y: v.dy = run.relative ? v.dy + value : value; break;
        }
    }
    return v;
}
float animation_cue_time(const AnimationSet& set, int value, float fallback) {
    for (const auto& cue : set.cues) if (!cue.complete && cue.value == value) return cue.time;
    return fallback;
}
AnimationDrop animation_drop(float from, float to, float time, float velocity, float acceleration, bool grow) {
    AnimationDrop d{from, grow ? 0.1f : 1.0f, 0};
    const int ticks = std::clamp(static_cast<int>(std::max(0.0f, time) * 60), 0, 600);
    float y = from;
    for (int tick = 0; tick < 600; ++tick) {
        const float scale = grow && to != from ? std::clamp((y - from) / (to - from) + 0.1f, 0.0f, 1.0f) : 1;
        velocity += acceleration;
        y += static_cast<int>(velocity);
        const bool landed = acceleration >= 0 ? y >= to : y <= to;
        if (tick < ticks) { d.y = landed ? to : y; d.scale = scale; }
        d.seconds = (tick + 1) / 60.0f;
        if (landed) break;
    }
    return d;
}
}
