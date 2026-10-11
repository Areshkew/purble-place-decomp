#include "pple/animation.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
namespace {
void require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
bool near(float a, float b) { return std::fabs(a-b) < 0.001f; }
pple::AnimationSet parse(const std::string& xml) {
    pple::AnimationSet set;
    require(pple::animation_parse({xml.begin(), xml.end()}, set), "parse timeline");
    return set;
}
}
int main() {
    const auto position = parse(R"(<Root><Animation><Affects>Position</Affects>
        <Type Name="LinearRelative"><Count>128</Count><Time>1.4</Time></Type>
        <Type Name="LinearRelative"><Count>-272</Count><Time>1.4</Time></Type>
        </Animation></Root>)");
    auto v = pple::animation_value(position, 0.7f);
    require(near(position.total, 1.4f) && near(v.dx, 64) && near(v.dy, -136), "Position axes run together and differ");
    const auto parallel = parse(R"(<Root><Animation><Affects>XPosition</Affects>
        <Type Name="LinearRelative"><Count>10</Count><Time>0.5</Time></Type></Animation>
        <Animation><Affects>Alpha</Affects><Type Name="LinearAbsolute"><Start>0</Start><End>200</End><Time>1</Time></Type></Animation>
        <WaitForComplete/><Animation><Affects>XPosition</Affects>
        <Type Name="LinearRelative"><Count>-10</Count><Time>0.5</Time></Type></Animation></Root>)");
    v = pple::animation_value(parallel, 0.25f);
    require(near(parallel.total, 1.5f) && near(v.dx, 5) && near(v.alpha, 50), "parallel runs until barrier");
    v = pple::animation_value(parallel, 1.25f);
    require(near(v.dx, 5) && near(v.alpha, 200), "relative runs accumulate after barrier");
    v = pple::animation_value(parallel, 0, true);
    require(near(v.dx, 0) && near(v.alpha, 200), "reverse starts at timeline end");
    const auto stream = parse(R"(<Root><Animation><Affects>CurrentFrame</Affects><Type Name="LinearAbsolute">
        <Start>0</Start><End>7</End><Time>0.35</Time></Type><IntMessage>2</IntMessage><IntMessage>7</IntMessage></Animation>
        <WaitForComplete/><Animation><Affects>CurrentFrame</Affects><Type Name="LinearAbsolute">
        <Start>7</Start><End>7</End><Time>0.25</Time></Type><SendComplete>3</SendComplete></Animation></Root>)");
    require(near(stream.total, 0.6f) && near(pple::animation_cue_time(stream, 2, -1), 0.1f), "message crosses frame two at 0.1 seconds");
    require(near(stream.cues.back().time, 0.6f) && stream.cues.back().complete, "sprite-state complete cue follows barrier");
    const auto drop = pple::animation_drop(-220, -65, 0, 4, 1, true);
    require(near(drop.y, -220) && drop.seconds > 0 && drop.seconds < 0.5f, "pan begins hidden and drop duration is independent of sample time");
    const auto landed = pple::animation_drop(-220, -65, 2, 4, 1, true);
    require(near(landed.y, -65) && near(landed.seconds, drop.seconds), "integer 60Hz drop clamps at landing");
    std::puts("PASS: parallel timelines, barriers, two-axis position, frame cues, reverse and pan timer");
}
