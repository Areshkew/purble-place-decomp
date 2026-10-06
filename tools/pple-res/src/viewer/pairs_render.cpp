#include "app.hpp"

namespace pple::viewer {

void App::refresh_cards() {
    if (!pairs_ready) return;
    scene_draws.assign(pairs_scene_layer.begin(), pairs_scene_layer.end());
    float extent_w = scene_w, extent_h = scene_h;
    for (std::size_t i = 0; i < card_cache.size(); ++i) {
        CardCache& entry = card_cache[i];
        const bool is_highlight = (i + 1 == card_cache.size());
        if (is_highlight) {
            const int pending = pairs_board.pending_reveal();
            if (pending < 0 || pending >= pairs_board.cell_count()) continue;
            if (entry.tex_face == nullptr) continue;
            const pple::ConcentrationSlot& slot = pairs_board.slots()[pending];
            const CardFlip& f = card_flip[pending];
            const float sx = static_cast<float>(f.scale_x) / 100.0f;
            const float sy = static_cast<float>(f.scale_y) / 100.0f;
            const float base_x = pairs_origin_x + static_cast<float>(slot.x);
            const float base_y = pairs_origin_y + static_cast<float>(slot.y);
            DrawItem d;
            d.texture = entry.tex_face;
            d.src = entry.src_face;
            d.dst = SDL_FRect{
                base_x + static_cast<float>((100.0f - static_cast<float>(f.scale_x)) / 100.0f *
                             static_cast<float>(slot.w) * 0.5f),
                base_y + static_cast<float>(fabs(100.0f - static_cast<float>(f.scale_y)) / 100.0f *
                             static_cast<float>(slot.h) * -1.0f),
                entry.w * sx, entry.h * sy};
            scene_draws.push_back(d);
            extent_w = std::max(extent_w, d.dst.x + d.dst.w);
            extent_h = std::max(extent_h, d.dst.y + d.dst.h);
            continue;
        }
        if (entry.index < 0 || entry.index >= pairs_board.cell_count()) continue;
        const pple::CardFace face = pairs_board.face(entry.index);
        const CardFx& fx = card_fx[static_cast<std::size_t>(entry.index)];
        // Card::OnHide (0x10007F538): a matched card's art is hidden and the level's
        // "BlankCard" (Card+0xB0, blankcardbutton.xml) is shown in its place.
        if (fx.blank && pairs_blank_tex != nullptr) {
            const float cs =
                static_cast<float>(pairs_board.slots()[static_cast<std::size_t>(entry.index)]
                                       .scale_percent) /
                100.0f;
            DrawItem d;
            d.texture = pairs_blank_tex;
            d.src = pairs_blank_src;
            d.dst = SDL_FRect{entry.base_x - pairs_blank_reg_x * cs,
                              entry.base_y - pairs_blank_reg_y * cs, pairs_blank_w * cs,
                              pairs_blank_h * cs};
            d.alpha = 255;
            scene_draws.push_back(d);
            extent_w = std::max(extent_w, d.dst.x + d.dst.w);
            extent_h = std::max(extent_h, d.dst.y + d.dst.h);
            continue;
        }
        const CardFlip& f = card_flip[entry.index];
        // Which side is on screen: mid-flip it is the side being collapsed, otherwise the
        // board's own face state decides.
        bool show_face = face != pple::CardFace::Hidden;
        if (f.phase == FlipPhase::Collapse) show_face = !f.to_face;
        else if (f.phase == FlipPhase::Expand) show_face = f.to_face;
        SDL_Texture* tex = show_face ? entry.tex_face : entry.tex_back;
        const SDL_FRect& src = show_face ? entry.src_face : entry.src_back;
        if (tex == nullptr) continue;
        // A matched card's flip-back (Card::FlipBack 0x10007D060 -> Card::UpdateFlipBackward
        // 0x10007EFEC) narrows the face the same way a forward flip does, but there is no
        // other side to expand: Card::OnHide ends it.  `fx.flip` carries that scale.
        int scale_x = f.scale_x, scale_y = f.scale_y;
        if (fx.flip > 0) {
            scale_x = std::max(15, 100 - 17 * fx.flip);
            scale_y = 100 + 2 * fx.flip;
        }
        // The engine recentres the narrowing card on the *cell*, not on the sprite:
        // SetPosition(node, fabs(100 - xscale)/100 * card.w * 0.5, ... * card.h * -1.0).
        const float off_x = (100.0f - static_cast<float>(scale_x)) / 100.0f * entry.cell_w * 0.5f;
        const float off_y = fabs(100.0f - static_cast<float>(scale_y)) / 100.0f *
                            entry.cell_h * -1.0f;
        DrawItem d;
        d.texture = tex;
        d.src = src;
        d.dst = SDL_FRect{entry.base_x + off_x, entry.base_y + off_y,
                          entry.w * static_cast<float>(scale_x) / 100.0f,
                          entry.h * static_cast<float>(scale_y) / 100.0f};
        d.alpha = 255;
        scene_draws.push_back(d);
        extent_w = std::max(extent_w, d.dst.x + d.dst.w);
        extent_h = std::max(extent_h, d.dst.y + d.dst.h);
    }
    // The explosion sparks.  Card::InitExplosion (0x10007E518) gives each of the four
    // Card+0x110 sparks a source at one corner of the card's cell and a destination 30 px
    // outside it, and Card::AnimateExplosion (0x10007E6D0) walks them for 30 ticks:
    //   position = src + (dst - src) * (3t^2(1-t) + t^4)
    //   scale    = 300t(1-t)^2 + 100(1-t)^3 + 600t^2(1-t) + 300t^4   percent
    // and from tick 15 it takes 10 off the sprite's own counter each tick, which is the fade.
    if (pairs_spark_tex != nullptr) {
        for (std::size_t i = 0; i < card_fx.size() && i < card_cache.size(); ++i) {
            const CardFx& fx = card_fx[i];
            if (fx.spark_tick < 0 || fx.spark_tick >= 30) continue;
            const CardCache& entry = card_cache[i];
            if (entry.index < 0) continue;
            const double t = static_cast<double>(fx.spark_tick) / 30.0;
            const double u = 1.0 - t;
            const double ease = 3.0 * t * t * u + t * t * t * t;
            const double scale =
                (300.0 * t * u * u + 100.0 * u * u * u + 600.0 * t * t * u + 300.0 * t * t * t * t) /
                100.0;
            const float alpha =
                static_cast<float>(std::clamp(100.0 - std::max(0, fx.spark_tick - 15) * 10.0, 0.0,
                                              100.0)) /
                100.0f;
            // The corners, in PlayArea space: the sparks are children of Board+0x78
            // (<PlayArea>), which is where the cards are too.
            const float cw = entry.cell_w, ch = entry.cell_h;
            const float src_x[4] = {entry.base_x, entry.base_x + cw, entry.base_x + cw,
                                    entry.base_x};
            const float src_y[4] = {entry.base_y, entry.base_y, entry.base_y + ch,
                                    entry.base_y + ch};
            const float dst_x[4] = {entry.base_x - 30.0f, entry.base_x + cw + 30.0f,
                                    entry.base_x + cw + 30.0f, entry.base_x - 30.0f};
            const float dst_y[4] = {entry.base_y - 30.0f, entry.base_y - 30.0f,
                                    entry.base_y + ch + 30.0f, entry.base_y + ch + 30.0f};
            for (int s = 0; s < 4; ++s) {
                const float px = static_cast<float>(src_x[s] + (dst_x[s] - src_x[s]) * ease);
                const float py = static_cast<float>(src_y[s] + (dst_y[s] - src_y[s]) * ease);
                DrawItem d;
                d.texture = pairs_spark_tex;
                d.src = pairs_spark_src;
                d.dst = SDL_FRect{px - pairs_spark_reg_x * static_cast<float>(scale),
                                  py - pairs_spark_reg_y * static_cast<float>(scale),
                                  pairs_spark_w * static_cast<float>(scale),
                                  pairs_spark_h * static_cast<float>(scale)};
                d.alpha = static_cast<int>(alpha * 255.0f);
                scene_draws.push_back(d);
                extent_w = std::max(extent_w, d.dst.x + d.dst.w);
                extent_h = std::max(extent_h, d.dst.y + d.dst.h);
            }
        }
    }
    // PlayArea precedes JarContainer. Move the cached card/spark layer before JarBase,
    // leaving the whole jar in front of the cards (Jar::InitUI, 0x100083218).
    // Rotate in place: no scene rebuild or per-tick texture/resource loading.
    const std::size_t card_draw_count = scene_draws.size() - pairs_scene_draws;
    if (pairs_jar_base_at >= 0 &&
        static_cast<std::size_t>(pairs_jar_base_at) < pairs_scene_draws) {
        std::rotate(scene_draws.begin() + pairs_jar_base_at,
                    scene_draws.begin() + static_cast<std::ptrdiff_t>(pairs_scene_draws),
                    scene_draws.end());
    }
    // SneakPeekContainer precedes JarContainer. Its movie is reset/hidden until a
    // token is spent, and token i is visible iff i + 1 <= Round+0x11C.
    std::vector<DrawItem> sneak_layer;
    if (pairs_sneak_movie_ok && pairs_sneak_frame >= 0.0f) {
        append_movie(pairs_sneak_movie, static_cast<int>(pairs_sneak_frame),
                     pairs_sneak_x, pairs_sneak_y, sneak_layer, extent_w, extent_h);
    }
    for (std::size_t i = 0; i < pairs_token_draws.size() &&
                            i < static_cast<std::size_t>(std::max(0, pairs_board.tokens())); ++i)
        sneak_layer.push_back(pairs_token_draws[i]);
    const std::size_t sneak_draw_count = sneak_layer.size();
    const std::size_t sneak_at = pairs_jar_base_at >= 0
        ? std::min(scene_draws.size(), static_cast<std::size_t>(pairs_jar_base_at) + card_draw_count)
        : scene_draws.size();
    scene_draws.insert(scene_draws.begin() + static_cast<std::ptrdiff_t>(sneak_at),
                       sneak_layer.begin(), sneak_layer.end());
    // The progress meter: indicator i shows its filled frame while
    // `i < Round+0x124`, which is ConRoundData's Level index -- the number of levels of this
    // difficulty already cleared.
    for (std::size_t i = 0; i < pairs_meter.size(); ++i) {
        const MeterDraw& m = pairs_meter[i];
        if (m.tex == nullptr) continue;
        const bool lit = static_cast<int>(i) < pairs_board.level_position();
        DrawItem d;
        d.texture = m.tex;
        d.src = lit ? m.src_lit : m.src_unlit;
        d.dst = m.dst;
        d.alpha = 255;
        scene_draws.push_back(d);
        extent_w = std::max(extent_w, d.dst.x + d.dst.w);
        extent_h = std::max(extent_h, d.dst.y + d.dst.h);
    }
    // The jar's flying coins.  ArcBehavior::Update (0x100084128) walks a quadratic Bezier
    // from the source through the apex to the destination with `(1-t)^2`, `2(1-t)t`, `t^2`,
    // `t = tick / /ArcAnimation/Frames`, and interpolates the node's scale over the same
    // curve from `s` to `2s` to `s/2` (ArcBehavior::Init's three Array adds).  The alpha
    // (`node+0x24`) grows by 0.2 per tick.
    // They are children of the "ArcContainer" Base, which Jar::InitUI creates *before* the
    // Base that holds the tray coins, so they draw behind the tray and in front of /JarBkg.
    std::vector<DrawItem> jar_layer;
    // JarMovieContainer is created before ArcContainer and the tray Base.
    const BurstMovie* burst =
        pairs_jar_phase == JarPhase::Burst ? load_burst(pairs_burst_name) : nullptr;
    if (burst != nullptr) {
        append_movie(burst->movie, static_cast<int>(pairs_burst_frame), pairs_burst_anchor_x,
                     pairs_burst_anchor_y, jar_layer, extent_w, extent_h);
    }
    for (const ArcCoin& a : pairs_arcs) {
        if (a.tex == nullptr) continue;
        double t = static_cast<double>(a.tick) / static_cast<double>(pairs_arc_frames);
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        const double u = 1.0 - t;
        const double w0 = u * u, w1 = 2.0 * u * t, w2 = t * t;
        const float px = static_cast<float>(w0 * a.sx + w1 * a.cx + w2 * a.dx);
        const float py = static_cast<float>(w0 * a.sy + w1 * a.cy + w2 * a.dy);
        const float scale =
            static_cast<float>(w0 * a.scale0 + w1 * (2.0 * a.scale0) + w2 * (a.scale0 * 0.5)) /
            100.0f;
        DrawItem d;
        d.texture = a.tex;
        d.src = a.src;
        d.dst = SDL_FRect{px - a.reg_x * scale, py - a.reg_y * scale, a.sprite_w * scale,
                          a.sprite_h * scale};
        d.alpha = static_cast<int>(std::clamp(0.2f * static_cast<float>(a.tick), 0.0f, 1.0f) *
                                   255.0f);
        jar_layer.push_back(d);
        extent_w = std::max(extent_w, d.dst.x + d.dst.w);
        extent_h = std::max(extent_h, d.dst.y + d.dst.h);
    }
    // The jar's tray.  Jar::DoFiller (0x100083B10):
    //   want = (int)(points / Board+0x28 * Round+0x12C)
    // Board+0x28 and Round+0x12C are both the *current* level's numbers, so the fill is that
    // level's fraction and Jar+0xC8 restarts at each level.  `pairs_jar_fill` is grown when a
    // coin lands, which is when the engine calls DoFiller (ArcBehavior::Update's tail), not
    // when the match is made.  The coins go between /JarBkg and /JarFg.
    if (burst != nullptr) {
        // Jar::Update's wipe: as the burst runs the tray's top coins are hidden, from
        // `v17 = n - 1 - frame * ((n + 2) / frameCount)` upwards, and nothing ever shows them
        // again -- the burst movie draws the coins erupting itself.
        const double n = static_cast<double>(pairs_jar_fill);
        const double v17 = n - 1.0 - static_cast<double>(pairs_burst_frame) *
                                       ((n + 2.0) /
                                        std::max(1.0, static_cast<double>(
                                                          burst->movie.frame_count)));
        const int left = v17 > 0.0 ? static_cast<int>(std::ceil(v17)) : 0;
        if (left < pairs_jar_fill) pairs_jar_fill = left;
    }
    for (int i = 0; i < pairs_jar_fill && i < static_cast<int>(pairs_jar_coins.size()); ++i) {
        const JarCoin& c = pairs_jar_coins[static_cast<std::size_t>(i)];
        if (c.tex == nullptr) continue;
        DrawItem d;
        d.texture = c.tex;
        d.src = c.src;
        // NodeSprite::UpdateZero: the drawn corner is (x, y) - scale * registration / 100.
        d.dst = SDL_FRect{pairs_jar_origin_x + c.x - c.reg_x,
                          pairs_jar_origin_y + c.y - c.reg_y, c.w, c.h};
        d.alpha = 255;
        d.rotation = c.rotation;
        jar_layer.push_back(d);
    }
    if (!jar_layer.empty() && pairs_jar_front_at >= 0) {
        const std::size_t at = static_cast<std::size_t>(std::min<int>(
            pairs_jar_front_at + static_cast<int>(card_draw_count + sneak_draw_count),
            static_cast<int>(scene_draws.size())));
        scene_draws.insert(scene_draws.begin() + static_cast<std::ptrdiff_t>(at),
                           jar_layer.begin(), jar_layer.end());
    }
    // The time bar: segment i is lit while i >= elapsed, red for the last 900 ticks.
    const int pending = pairs_board.pending_reveal();
    (void)pending;
    if (timebar.ok) {
        const int total = pairs_board.level().total_ticks();
        const int left = static_cast<int>(pairs_board.ticks_remaining());
        int elapsed = 0;
        if (timebar.segments > 0 && total > 0) {
            const double per = static_cast<double>(total) / static_cast<double>(timebar.segments);
            if (per > 0.0) elapsed = static_cast<int>((total - left) / per);
        }
        if (elapsed < 0) elapsed = 0;
        if (elapsed > timebar.segments) elapsed = timebar.segments;
        const bool red = left < 900;
        SDL_Texture* tex = red ? timebar.red : timebar.green;
        const SDL_FRect* src = red ? &timebar.red_src : &timebar.green_src;
        const float x = red ? timebar.x_red : timebar.x_green;
        if (tex != nullptr) {
            for (int i = elapsed; i < timebar.segments; ++i) {
                DrawItem d;
                d.texture = tex;
                d.src = *src;
                d.dst = SDL_FRect{x, timebar.y_top + static_cast<float>(i),
                                  src->w, src->h};
                d.alpha = 255;
                scene_draws.push_back(d);
            }
        }
    }
    // Animated parts may leave the viewport; they must not enlarge its design bounds
    // (otherwise the whole scene progressively zooms out during the sneak/burst movies).
    scene_w = pairs_scene_width;
    scene_h = pairs_scene_height;
    // The Professor: the movie is chosen by the state machine in professor.cpp (Professor's own
    // Update 0x10008C088), not by the board flags, because a win or a loss is a tip sequence:
    // Pro_Win / Pro_Lose while the bubble is up, then Pro_Exit, and only then the score panel.
    if (professor.ok) {
        std::vector<pple::MoviePlacement> placements;
        const float fps =
            professor.movie.parts.empty()
                ? 12.0f
                : pple::movie_frame_rate(professor.movie, professor.movie.parts.front());
        const int mframe =
            (professor.movie.frame_count > 1 && fps > 0.0f)
                ? static_cast<int>(professor.time * fps) % professor.movie.frame_count
                : 0;
        pple::movie_evaluate(professor.movie, mframe, placements);
        for (const pple::MoviePlacement& p : placements) {
            if (p.part == nullptr || !p.part->resolved || p.part->sprite.sheet.empty()) continue;
            std::string sheet_how;
            const auto* sheet_slot = bundle.resolve(p.part->sprite.sheet, sheet_how);
            if (sheet_slot == nullptr) continue;
            std::string note;
            SDL_Texture* tex =
                load_sheet_texture(p.part->sprite.sheet, sheet_slot->entry->name, note);
            if (tex == nullptr) continue;
            float sx = 0.0f, sy = 0.0f, sw = 0.0f, sh = 0.0f;
            pple::sprite_frame_rect(p.part->sprite, 0, sx, sy, sw, sh);
            DrawItem d;
            d.texture = tex;
            d.src = SDL_FRect{sx, sy, sw, sh};
            d.dst = SDL_FRect{professor.anchor_x + p.x - p.part->sprite.reg_x,
                              professor.anchor_y + p.y - p.part->sprite.reg_y,
                              sw * p.xscale, sh * p.yscale};
            d.alpha = static_cast<int>(std::clamp(p.alpha * 255.0f, 0.0f, 255.0f));
            d.rotation = p.rotation;
            scene_draws.push_back(d);
            extent_w = std::max(extent_w, d.dst.x + d.dst.w);
            extent_h = std::max(extent_h, d.dst.y + d.dst.h);
        }
        scene_w = pairs_scene_width;
        scene_h = pairs_scene_height;
    }
}

// --bench measures the card layer: refresh_cards is what replaced the per-tick build_pairs,
// so its cost per call is the number that matters.
void App::timed_refresh() {
    const Uint64 t0 = SDL_GetPerformanceCounter();
    refresh_cards();
    const Uint64 t1 = SDL_GetPerformanceCounter();
    const double ms = 1000.0 * static_cast<double>(t1 - t0) /
                      static_cast<double>(SDL_GetPerformanceFrequency());
    refresh_total_ms += ms;
    // The first call also decodes whatever sheets the cards need, so it is reported apart
    // from the steady-state cost: that priming call is what build_pairs' own timing covers.
    if (refresh_calls == 0) refresh_first_ms = ms;
    ++refresh_calls;
}

} // namespace pple::viewer
