#include "app.hpp"
#include <cstdlib>

namespace pple::viewer {

// The Professor's six movies and the state machine Professor::Update (0x10008C088) runs over
// them.  Every address below is from the shipped image:
//
//   Professor::InitUI      0x10008BD94  loads Pro_Enter / Pro_Exit / Pro_Talking / Pro_Idle /
//                                       Pro_Win / Pro_Lose as children of /ProfessorContainer
//   Professor::Hide        0x10008C034  state 1, resets all six
//   Professor::MoveIn      0x10008C354  from hidden, Pro_Enter, otherwise DisplayTip now
//   Professor::MoveOut     0x10008C3B0  Pro_Exit, state 3
//   Professor::DisplayTip  0x10008C290  picks Pro_Win (kind 2) / Pro_Lose (kind 1) / Pro_Talking
//   Professor::DisplayIntro 0x10008C3F0 ProIntro, or ProWelcome when the game was seen before
//
// The trigger sites are ConcentrationGame::OnEnterWonGameState (0x1000821D0, +0x4C = 1,
// +0x64 = 2, +0x48 = 150, a random ProWin1..3), OnEnterLoseGameState (0x10008259C, +0x64 = 1,
// a random ProLose1..2) and Jar::StopExplosion (0x100083A20, ProIntLevelChange /
// ProAdvLevelChange at +0x48 = 125).

namespace {
struct ProfessorMovieRef {
    const char* instance;
    const char* ref;
};
const ProfessorMovieRef kProfessorMovies[] = {
    {"ProfessorEnterAnimation", "UI\\Concentration\\Pro_Enter.xml"},
    {"ProfessorExitAnimation", "UI\\Concentration\\Pro_Exit.xml"},
    {"ProfessorTalkAnimation", "UI\\Concentration\\Pro_Talking.xml"},
    {"ProfessorIdleAnimation", "UI\\Concentration\\Pro_Idle.xml"},
    {"ProfessorWinAnimation", "UI\\Concentration\\Pro_Win.xml"},
    {"ProfessorLoseAnimation", "UI\\Concentration\\Pro_Lose.xml"},
};
} // namespace

// One movie, loaded once and kept across board rebuilds.  The parts' sheet references are
// resolved lazily by the draw path, so caching the parsed document is all this costs.
bool App::load_professor_movie(const std::string& instance) {
    if (prof_movie_cache.count(instance) != 0) return true;
    const char* ref = nullptr;
    for (const ProfessorMovieRef& entry : kProfessorMovies)
        if (instance == entry.instance) ref = entry.ref;
    if (ref == nullptr) return false;
    std::string how;
    const auto* slot = bundle.resolve(ref, how);
    if (slot == nullptr) {
        std::printf("  professor %s: %s not found\n", instance.c_str(), ref);
        return false;
    }
    pple::Movie movie;
    std::string error;
    if (!pple::movie_load(bundle.read(*slot), fetch_resource, movie, error)) {
        std::printf("  professor %s: %s\n", instance.c_str(), error.c_str());
        return false;
    }
    prof_movie_cache.emplace(instance, std::move(movie));
    return true;
}

// PurblePlaceMovie::Show: the movie becomes the visible one and its clock restarts.
void App::professor_show(const std::string& instance, ProfPhase phase, bool looping) {
    if (!load_professor_movie(instance)) {
        professor.ok = false;
        return;
    }
    const bool changed = professor.which != instance;
    professor.movie = prof_movie_cache[instance];
    professor.which = instance;
    professor.phase = phase;
    professor.looping = looping;
    professor.time = 0.0f;
    professor.ok = true;
    if (changed) std::printf("  professor -> %s\n", instance.c_str());
}

bool App::load_professor(const std::string& instance) {
    professor_show(instance, ProfPhase::Idle, true);
    return professor.ok;
}

// DisplayTip: the movie comes from the animation kind Professor+0x64, and the tip name from
// +0x50 is what the tip manager is then asked for.
void App::professor_display_tip() {
    if (professor.anim_kind == 2) professor_show("ProfessorWinAnimation", ProfPhase::Win, false);
    else if (professor.anim_kind == 1) professor_show("ProfessorLoseAnimation", ProfPhase::Lose, false);
    else professor_show("ProfessorTalkAnimation", ProfPhase::Talking, true);
    if (!professor.ok) return;
    // --no-tips: the Professor still animates, but there is no bubble to wait for, so the intro
    // goes straight to Idle and a win or a loss is left to the results panel.
    if (!tips_enabled) {
        if (professor.anim_kind == 0) {
            professor_show("ProfessorIdleAnimation", ProfPhase::Idle, true);
        }
        return;
    }
    if (!professor.tip_name.empty()) show_tip(professor.tip_name, 615.0f, professor.tip_y);
}

// Both tip paths end here: the player closed the bubble, or its <Duration> ran out.  The intro
// is a two-bubble chain (Professor::Update's case 5: ProIntro then ProIntroNext, with +0x48
// 150 then 120, and Pro_Idle afterwards).
void App::professor_tip_closed() {
    const std::string closed = tip.id;
    hide_tip();
    if (professor.phase != ProfPhase::Talking) {
        // A win or a loss: the movie finishes first, then the exit animation runs (checked
        // every tick in professor_advance).
        return;
    }
    if (closed == "ProIntro") {
        professor.tip_name = "ProIntroNext";
        professor.tip_y = 120.0f;
        professor_show("ProfessorTalkAnimation", ProfPhase::Talking, true);
        if (professor.ok) show_tip(professor.tip_name, 615.0f, professor.tip_y);
        return;
    }
    professor_show("ProfessorIdleAnimation", ProfPhase::Idle, true);
}

// Jar::StopExplosion sets the name and +0x48 = 125 and calls MoveIn, while Board::NewLevel
// re-deals the next <Level> behind the tip.
void App::professor_level_tip(const std::string& id) {
    professor.anim_kind = 0;
    professor.tip_name = id;
    professor.tip_y = 125.0f;
    if (professor.phase == ProfPhase::Hidden)
        professor_show("ProfessorEnterAnimation", ProfPhase::Enter, false);
    else
        professor_display_tip();
}

// OnEnterWonGameState / OnEnterLoseGameState: the tip name and +0x64, then MoveIn.
void App::professor_round_end(bool won) {
    professor.anim_kind = won ? 2 : 1;
    const int pick = std::rand() % (won ? 3 : 2);
    professor.tip_name = won ? "ProWin" + std::to_string(pick + 1)
                             : "ProLose" + std::to_string(pick + 1);
    professor.tip_y = 150.0f;
    if (professor.phase == ProfPhase::Hidden)
        professor_show("ProfessorEnterAnimation", ProfPhase::Enter, false);
    else
        professor_display_tip();
}

// DisplayIntro: from hidden, Pro_Enter plays first and DisplayTip follows when it stops.
void App::professor_intro_begin() {
    professor.anim_kind = 0;
    professor.tip_name = "ProIntro";
    professor.tip_y = 150.0f;
    professor_show("ProfessorEnterAnimation", ProfPhase::Enter, false);
}

// One 30 Hz step.  The movie clock and the tip's own <Duration> both run here; the win and the
// loss leave once the movie has stopped *and* the tip is gone, which is Professor::Update's
// condition for states 6 and 7.
void App::professor_advance() {
    if (tip.visible) {
        if (!tip.wait_click) {
            tip.seconds_left -= 1.0f / 30.0f;
            if (tip.seconds_left <= 0.0f) professor_tip_closed();
        }
    }
    if (!professor.ok || professor.movie.parts.empty()) return;
    professor.time += 1.0f / 30.0f;
    const float fps = pple::movie_frame_rate(professor.movie, professor.movie.parts.front());
    const float frames = static_cast<float>(std::max(1, professor.movie.frame_count));
    if (professor.looping) {
        // Pro_Idle and Pro_Talking are cycles, so the clock wraps instead of stopping.
        if (fps > 0.0f && professor.time * fps >= frames) professor.time -= frames / fps;
        return;
    }
    const bool finished = fps <= 0.0f || professor.time * fps >= frames;
    switch (professor.phase) {
        case ProfPhase::Enter:
            // Case 2: the entrance has played, so the tip and the talking loop follow.
            if (finished) professor_display_tip();
            break;
        case ProfPhase::Win:
        case ProfPhase::Lose:
            // Cases 6 and 7: Hide + MoveOut once the movie has stopped and no tip is up.
            if (finished && !tip.visible) {
                const bool won = professor.anim_kind == 2;
                const bool lost = professor.anim_kind == 1;
                professor_show("ProfessorExitAnimation", ProfPhase::Exit, false);
                if (!won && !lost) professor.phase = ProfPhase::Hidden;
            }
            break;
        case ProfPhase::Exit:
            // Case 3: the exit finished, so the game's own OnWinAnimStop / OnLoseAnimStop runs.
            if (!finished) break;
            professor.ok = false;
            professor.phase = ProfPhase::Hidden;
            if (professor.anim_kind == 2) build_result_panel(true);
            else if (professor.anim_kind == 1) build_result_panel(false);
            professor.anim_kind = 0;
            break;
        default:
            break;
    }
}

} // namespace pple::viewer
