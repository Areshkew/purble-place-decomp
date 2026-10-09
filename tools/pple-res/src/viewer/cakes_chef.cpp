#include "app.hpp"
#include "audio.hpp"
#include "pple/cakes.hpp"

#include <cstdio>

namespace pple::viewer {

// The Purble Chef.  ComfyCakesGame::InitGameUI creates him last of all the room's nodes
// (0x10005E0B7), PurbleChef::Init (0x10006F7DC) builds a Base under the game container and loads
// the six movies the room document names, and PurbleChef::Show (0x10006FD1C) queues a target
// state with the tip that goes with it.  The state numbers come from PurbleChef::OnEnterState
// (0x10006FFEC):
//
//   0 hidden       Reset: StopEverything, clear the queue
//   1 idle         movie[8]  /IdleMoviePath
//   2 entering     movie[6]  /EnterMoviePath      (GameAudio proto 0x1A, Con_ChefEnter)
//   3 leaving      movie[7]  /LeaveMoviePath      and the tip is hidden
//   4 talking      movie[9]  /TalkMoviePath       (proto 0x19)
//   5 win          movie[10] /WinMoviePath        (proto 0x19)
//   6 lose         movie[11] /LoseMoviePath       (proto 0x19)
//
// Show() pushes its record to the front of the queue and, when the Chef is hidden, enters state 2
// first; PopPendingShow then plays the queued state.  A movie that completes returns to idle, and
// Idle loops.

namespace {

// The movie basename -> the state whose OnEnterState shows it.
struct ChefMovie {
    const char* name;
    int state;
    bool loops;
};
const ChefMovie kChefMovies[] = {
    {
        "Idle", 1, true
    },
    {
        "Enter", 2, false
    },
    {
        "Leave", 3, false
    },
    {
        "Talk", 4, false
    },
    {
        "Win", 5, false
    },
    {
        "Lose", 6, false
    },
};

// The Chef's own Base is put at (320,80) by PurbleChef::Init and then SetWidgetPos moves it to
// the room document's <PurbleChef> X/Y of 900,400.  His movies are children of it, and
// ChefIdleMovie.xml's first frame places his parts at x 643..690, y 401..442 -- inside the room,
// at the bottom right where the Chef stands -- so the movies are anchored at the room origin and
// the (900,400) position is not what draws them.
constexpr float kChefAnchorX = 0.0f;
constexpr float kChefAnchorY = 0.0f;

// Where the Chef's tips sit.  CCTipManager::ShowSimpleTip puts the Comfy Cakes bubbles on the
// Chef's own node; the room document's tip container is at 615,125 for Purble Pairs, and the
// Comfy Cakes tips are the same 350-px-wide panels.
constexpr float kChefTipX = 615.0f;
constexpr float kChefTipY = 150.0f;

}  // namespace

// The six movies, parsed once, exactly as professor.cpp does for the Professor.
void App::cakes_load_chef() {
    if (cakes_chef_loaded) return;
    cakes_chef_loaded = true;
    const auto fetch = [&](const std::string& name, std::vector<std::uint8_t>& out) {
        std::string how;
        const auto* s = bundle.resolve(name, how);
        if (s == nullptr) return false;
        out = bundle.read(*s);
        return true;
    };
    // His bubbles come from the game's own document, merged into the same table the Pairs tips
    // live in.
    load_tips("UI\\COMFYCAKES\\COMFYCAKESTIPS.XML");
    for (const ChefMovie& entry : kChefMovies) {
        const std::string path =
            std::string("UI\\ComfyCakes\\Chef\\Chef") + entry.name + "Movie.xml";
        std::vector<std::uint8_t> bytes;
        pple::Movie movie;
        std::string error;
        if (!fetch(path, bytes) || !pple::movie_load(bytes, fetch, movie, error) ||
            movie.parts.empty()) {
            std::printf("cakes chef: %s, %s\n", path.c_str(), error.c_str());
            continue;
        }
        cakes_chef_movies[entry.name] = std::move(movie);
        std::printf("cakes chef: %s, %d frame(s) at %.0f fps, %zu part(s)\n", path.c_str(),
                    cakes_chef_movies[entry.name].frame_count,
                    static_cast<double>(pple::movie_frame_rate(
                        cakes_chef_movies[entry.name], cakes_chef_movies[entry.name].parts.front())),
                    cakes_chef_movies[entry.name].parts.size());
    }
}

// PurbleChef::Show (0x10006FD1C) with the tip that goes with the state.  A request while the Chef
// is hidden enters him first, which is what the 0x10006FE1D branch does.
void App::cakes_chef_show(int state, const std::string& tip_id) {
    if (state <= 0) {
        cakes_chef_state = 0;
        cakes_chef_pending = -1;
        cakes_chef_pending_tip.clear();
        cakes_chef_frame = -1.0f;
        if (tip.visible) hide_tip();
        std::printf("cakes chef: reset\n");
        return;
    }
    if (cakes_chef_state != 0) {
        cakes_chef_pending = state;
        cakes_chef_pending_tip = tip_id;
        std::printf("cakes chef: queued state %d (%s)\n", state, tip_id.c_str());
        return;
    }
    // Hidden: enter, then pop the queued show when the entrance finishes.
    cakes_chef_pending = state;
    cakes_chef_pending_tip = tip_id;
    cakes_chef_enter();
}

// PurbleChef::OnEnterState's per-state work: stop the other movies, show this one, play its sound.
void App::cakes_chef_enter() {
    cakes_chef_state = 2;
    cakes_chef_frame = 0.0f;
    const pple::Movie* enter = cakes_chef_movie("Enter");
    if (enter == nullptr) return;
    const int fps = static_cast<int>(pple::movie_frame_rate(*enter, enter->parts.front()));
    if (fps > 0) cakes_chef_fps = static_cast<float>(fps);
    std::printf("cakes chef: entering\n");
    play_pair_sfx("Con_ChefEnter");
}

const pple::Movie* App::cakes_chef_movie(const char* which) const {
    const auto it = cakes_chef_movies.find(which);
    return it == cakes_chef_movies.end() ? nullptr : &it->second;
}

// PurbleChef::Update (0x10006FF44) on the 30 Hz tick: advance the current movie, and when a
// one-shot movie finishes, either pop the queued show or fall back to the looping Idle.
void App::cakes_chef_advance() {
    if (cakes_chef_state == 0) return;
    const char* which = "Idle";
    bool loops = true;
    switch (cakes_chef_state) {
        case 2: which = "Enter"; loops = false; break;
        case 3: which = "Leave"; loops = false; break;
        case 4: which = "Talk"; loops = false; break;
        case 5: which = "Win"; loops = false; break;
        case 6: which = "Lose"; loops = false; break;
        default: break;
    }
    const pple::Movie* movie = cakes_chef_movie(which);
    if (movie == nullptr) return;
    const int frames = std::max(1, movie->frame_count);
    const float fps = pple::movie_frame_rate(*movie, movie->parts.front());
    cakes_chef_frame += fps / 30.0f;
    if (cakes_chef_frame < static_cast<float>(frames)) return;
    // The movie ran out.
    if (loops) {
        cakes_chef_frame = 0.0f;
        return;
    }
    if (cakes_chef_state == 2 && cakes_chef_pending > 0) {
        // The entrance finished: play the queued state and its tip.
        const int next = cakes_chef_pending;
        const std::string tip_id = cakes_chef_pending_tip;
        cakes_chef_pending = -1;
        cakes_chef_pending_tip.clear();
        cakes_chef_state = next;
        cakes_chef_frame = 0.0f;
        play_pair_sfx(next == 6 ? "Con_ChefExit" : "Con_TVon_Ding");
        std::printf("cakes chef: state %d (%s)\n", next, tip_id.c_str());
        if (!tip_id.empty()) show_tip(tip_id, kChefTipX, kChefTipY);
        return;
    }
    // Everything else falls back to the idle loop.
    cakes_chef_state = 1;
    cakes_chef_frame = 0.0f;
    if (tip.visible) hide_tip();
}

// Draw the Chef's current movie over the room.
void App::cakes_draw_chef() {
    if (cakes_chef_state == 0) return;
    const char* which = "Idle";
    switch (cakes_chef_state) {
        case 2: which = "Enter"; break;
        case 3: which = "Leave"; break;
        case 4: which = "Talk"; break;
        case 5: which = "Win"; break;
        case 6: which = "Lose"; break;
        default: break;
    }
    const pple::Movie* movie = cakes_chef_movie(which);
    if (movie == nullptr) return;
    const int frames = std::max(1, movie->frame_count);
    int frame = cakes_chef_frame < 0.0f ? 0 : static_cast<int>(cakes_chef_frame);
    if (which == "Idle") frame %= frames;
    else frame = std::min(frame, frames - 1);
    const std::size_t before = scene_draws.size();
    float extent_w = 0.0f, extent_h = 0.0f;
    append_movie(*movie, frame, kChefAnchorX, kChefAnchorY, scene_draws, extent_w, extent_h);
    for (std::size_t i = before; i < scene_draws.size(); ++i)
        cakes_draw_names.push_back(std::string("Chef") + which);
}

}  // namespace pple::viewer
