#include "../src/viewer/cakes_clock.hpp"
#include <cstdio>
#include <cstdlib>

void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    using pple::viewer::CakeClock;
    pple::CakeVars vars;
    vars.belt_slide_time = 5;
    vars.num_slides_to_add_cake = 7;
    CakeClock clock;
    require(!clock.advance(10, 1, false, vars) && clock.elapsed == 0, "initial cake waits for first tool");
    clock.started = true;
    require(!clock.advance(10, 0, false, vars) && clock.elapsed == 0, "Beginner has no automatic belt");
    require(!clock.advance(10, 1, true, vars) && clock.elapsed == 0, "machine and motion pause clock");
    require(!clock.advance(2.5f, 1, false, vars), "Intermediate waits five seconds");
    require(clock.indicator_offset(vars) == 16, "indicator halfway through its 32px travel");
    require(clock.advance(2.5f, 1, false, vars), "Intermediate reaches deadline");
    require(clock.indicator_offset(vars) == 32, "indicator reaches end");
    clock.slid(false);
    require(clock.elapsed == 0 && clock.slides == 1, "manual slide resets clock and counts");
    clock.slides = 6;
    require(!clock.add_due(1, vars), "do not spawn while inlet unavailable");
    clock.entry_clear = true;
    require(clock.add_due(1, vars), "seventh right slide adds paper");
    require(!clock.add_due(0, vars), "Beginner only replaces judged cake");
    clock.added();
    require(clock.slides == 0 && !clock.entry_clear, "spawn resets schedule");
    clock.slid(true);
    require(clock.slides == 0, "automatic count waits for clear inlet");
    clock.entry_clear = true;
    clock.slid(true);
    require(clock.slides == 1, "automatic count resumes after clear inlet");
    vars.belt_slide_time = 4;
    clock.elapsed = 0;
    require(!clock.advance(3.99f, 2, false, vars), "Advanced waits four seconds");
    require(clock.advance(0.02f, 2, false, vars), "Advanced deadline");
    clock = {};
    require(clock.elapsed == 0 && !clock.started && !clock.entry_clear && clock.slides == 0,
            "restart discards old clock and spawn schedule");
    std::puts("PASS: conveyor clock, indicator, pause, spawn cadence and reset");
}
