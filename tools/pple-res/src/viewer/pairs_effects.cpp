#include "app.hpp"
#include "audio.hpp"

namespace pple::viewer {

// Loads a level's burst movie once and caches it.  Jar::LoadMovies does this at Jar::InitUI,
// but the document is 250 KB of keyframes (88 frames, 322 parts), so it is deferred to the
// moment the burst starts rather than paid on every scene rebuild.
const BurstMovie* App::load_burst(const std::string& name) {
    if (name.empty()) return nullptr;
    const auto found = pairs_burst_cache.find(name);
    if (found != pairs_burst_cache.end()) return found->second.ok ? &found->second : nullptr;
    BurstMovie entry;
    std::vector<std::uint8_t> bytes;
    const auto fetch = [&](const std::string& part_name, std::vector<std::uint8_t>& out) {
        std::string how;
        const auto* s = bundle.resolve(part_name, how);
        if (s == nullptr) return false;
        out = bundle.read(*s);
        return true;
    };
    std::string error;
    if (fetch(name, bytes) && pple::movie_load(bytes, fetch, entry.movie, error)) {
        entry.ok = !entry.movie.parts.empty();
        const float fps = entry.movie.parts.empty()
                              ? entry.movie.fps
                              : pple::movie_frame_rate(entry.movie, entry.movie.parts.front());
        std::printf("  jar burst: %s, %d frame(s) at %.0f fps, %zu part(s), anchor %.0f,%.0f\n",
                    name.c_str(), entry.movie.frame_count, static_cast<double>(fps),
                    entry.movie.parts.size(), static_cast<double>(pairs_burst_anchor_x),
                    static_cast<double>(pairs_burst_anchor_y));
    } else {
        std::printf("  jar burst: %s did not load (%s)\n", name.c_str(), error.c_str());
    }
    const auto placed = pairs_burst_cache.emplace(name, std::move(entry));
    return placed.first->second.ok ? &placed.first->second : nullptr;
}

// Jar::Update (0x10008389D): the flying-coin array is empty and the board is complete, so
// play Con_BoardCleared (proto 0x6D), put /JarBkg behind everything and show the level's
// burst movie (Jar+0x30, from CardBacks.xml's /moviepath).
void App::jar_begin_burst() {
    if (!pairs_board.end_pending()) return;
    play_pair_sfx("Con_BoardCleared");
    pairs_burst_frame = 0.0f;
    const BurstMovie* burst = load_burst(pairs_burst_name);
    pairs_jar_phase = (burst != nullptr && burst->movie.frame_count > 0) ? JarPhase::Burst
                                                                        : JarPhase::Idle;
}

// Board::DoFlyIntoJar (0x10008AF54) once per matched card.  The source is the card's own
// position -- Card::Update (0x10007DAFA) passes the card node's (x, y), which is
// <PlayArea>-relative, and the function adds PlayArea's 150,2 back while subtracting
// /JarContainer's origin, so the arc runs in /JarContainer's space.
void App::spawn_arcs(const std::vector<int>& cells) {
    if (pairs_coin_tex == nullptr || cells.empty()) return;
    const float dx = pairs_jar_origin_x + pairs_arc_lid + pairs_curjar_w * 0.5f;
    const float dy = pairs_jar_origin_y + pairs_curjar_h * 0.5f;
    for (int cell : cells) {
        if (cell < 0 || cell >= static_cast<int>(pairs_board.slots().size())) continue;
        const pple::ConcentrationSlot& slot = pairs_board.slots()[static_cast<std::size_t>(cell)];
        ArcCoin c;
        c.tex = pairs_coin_tex;
        c.src = pairs_coin_src;
        c.sprite_w = pairs_coin_w;
        c.sprite_h = pairs_coin_h;
        c.reg_x = pairs_coin_reg_x;
        c.reg_y = pairs_coin_reg_y;
        c.sx = pairs_origin_x + static_cast<float>(slot.x);
        c.sy = pairs_origin_y + static_cast<float>(slot.y) + 2.0f;
        c.dx = dx;
        c.dy = dy;
        // ArcBehavior+0x28 is `card_y + 62` (0x10008B052) and the apex is
        // `min(src.y, dst.y) - ArcBehavior+0x28` (0x1000840E8) in the arc's own space.  The
        // source's y is `card_y + 2 - JarContainer.y`, so the two card_y terms cancel and the
        // control point's height above the container is a constant 375 px for every card at
        // or above the jar.  The control point is off the top of the 800x496 scene, but the
        // drawn curve is not: a quadratic Bezier only reaches about half way to its control
        // point, so the coin's highest drawn point stays on screen.
        c.cx = dx + (c.sx - dx) * 0.5f;
        c.cy = std::min(c.sy, c.dy) - static_cast<float>(slot.y) - 62.0f;
        c.scale0 = static_cast<float>(slot.scale_percent + 10);  // Board+0x170 + 10
        c.tick = 0;
        pairs_arcs.push_back(c);
    }
    if (!pairs_arcs.empty()) pairs_jar_phase = JarPhase::Flying;
}

// Jar::Update's landing: the tray grows to `points / Board+0x28 * Round+0x12C`.  DoFiller
// only ever adds nodes, so the count never falls within a level.
void App::jar_grow() {
    if (pairs_jar_coins.empty()) return;
    const int total_cards = std::max(1, pairs_board.cell_count());
    const double want = static_cast<double>(pairs_jar_coins.size()) /
                        static_cast<double>(total_cards) *
                        static_cast<double>(pairs_board.cards_removed());
    pairs_jar_fill = std::max(pairs_jar_fill, static_cast<int>(want));
}

// Card::DoForceMatch (0x10007D114) once per matched card: arm the countdown, start the
// explosion, and mark the card doomed.  `immediate` is for the scripted hooks, which have no
// frames to animate in, so Card::OnHide's end state is taken straight away.
void App::arm_match_fx(const std::vector<int>& cells, bool immediate) {
    for (int cell : cells) {
        if (cell < 0 || cell >= static_cast<int>(card_fx.size())) continue;
        CardFx& fx = card_fx[static_cast<std::size_t>(cell)];
        fx.doomed = true;
        fx.countdown = 10;    // Card::DoForceMatch's `*((_DWORD *)this + 15) = 10`
        fx.spark_tick = 0;    // Card::InitExplosion
        fx.flip = 0;
        if (immediate) {
            fx.countdown = -1;
            fx.spark_tick = -1;
            fx.blank = true;
            card_fx_pending_fly.push_back(cell);
        }
    }
    if (!immediate) return;
    // The scripted hooks have no frames to fly the coins in, so the tray takes the fill the
    // landings would have produced (Jar::DoFiller) and no arcs are spawned.
    card_fx_pending_fly.clear();
    jar_grow();
}

// ---- card flip animation ---------------------------------------------------------
// Card::FlipForward (0x10007CFA4) does not change the art: it sets the state to 3 and lets
// Card::UpdateFlipForward (0x10007EA20) animate, and Card::OnReveal (0x10007F824) only runs
// when the flip finishes.  The same shape here: a face change starts a flip, and the drawn
// side follows the phase.
void App::start_flip(int index, bool to_face) {
    if (index < 0 || index >= static_cast<int>(card_flip.size())) return;
    CardFlip& f = card_flip[static_cast<std::size_t>(index)];
    if (f.phase != FlipPhase::None) return;
    f.phase = FlipPhase::Collapse;
    f.scale_x = 100;
    f.scale_y = 100;
    f.to_face = to_face;
}

void App::sync_flips() {
    for (std::size_t i = 0; i < card_face_shown.size(); ++i) {
        const pple::CardFace now = pairs_board.face(static_cast<int>(i));
        if (now == card_face_shown[i]) continue;
        const bool was_hidden = card_face_shown[i] == pple::CardFace::Hidden;
        const bool is_hidden = now == pple::CardFace::Hidden;
        // A Hidden <-> shown change is a flip; Shown -> Matched is not, the card simply
        // stays up.
        if (was_hidden != is_hidden) start_flip(static_cast<int>(i), !is_hidden);
        card_face_shown[i] = now;
    }
}

bool App::advance_flips() {
    bool changed = false;
    for (CardFlip& f : card_flip) {
        if (f.phase == FlipPhase::Collapse) {
            f.scale_x -= 17;  // the visible side narrows at 17 percent per tick ...
            f.scale_y += 2;   // ... and drifts 2 taller
            if (f.scale_x <= 15) {  // the engine clamps the collapse at 15 percent
                f.scale_x = 15;
                f.phase = FlipPhase::Expand;
            }
            changed = true;
        } else if (f.phase == FlipPhase::Expand) {
            f.scale_x += 17;
            f.scale_y -= 2;
            if (f.scale_x >= 100) {
                f.scale_x = 100;
                f.scale_y = 100;
                f.phase = FlipPhase::None;
            }
            changed = true;
        }
    }
    return changed;
}

// The match animation's own clock, one step per 30 Hz tick: Card::AnimateExplosion's 30-tick
// explosion, Card::Update's countdown, and the flip-back that Card::OnHide ends.
bool App::advance_card_fx() {
    bool changed = false;
    for (std::size_t i = 0; i < card_fx.size(); ++i) {
        CardFx& fx = card_fx[i];
        if (fx.spark_tick >= 0) {
            ++fx.spark_tick;
            if (fx.spark_tick >= 30) fx.spark_tick = -1;
            changed = true;
        }
        if (fx.countdown > 0) {
            --fx.countdown;
            changed = true;
            if (fx.countdown == 0) fx.flip = 1;  // Card::FlipBack
        }
        if (fx.flip > 0 && !fx.blank) {
            ++fx.flip;
            changed = true;
            if (std::max(15, 100 - 17 * fx.flip) <= 15) {
                // Card::OnHide (0x10007F538): the art goes, the BlankCard takes its place,
                // and Board::DoFlyIntoJar sends the coin.
                fx.blank = true;
                card_fx_pending_fly.push_back(static_cast<int>(i));
            }
        }
    }
    if (!card_fx_pending_fly.empty()) {
        const std::vector<int> flying = card_fx_pending_fly;
        card_fx_pending_fly.clear();
        spawn_arcs(flying);
    }
    return changed;
}

} // namespace pple::viewer
