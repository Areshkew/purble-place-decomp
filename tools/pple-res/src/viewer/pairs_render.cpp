#include "app.hpp"

namespace pple::viewer {

// The time bar's drawn state: segment i is lit while i >= elapsed, and the bar swaps to
// <TimeBarRed> for the last 900 ticks.  Board::Update stops the clock once the board is complete,
// and a level with no <TotalTime> has `total_ticks` of 30 * -1, which cannot light a segment.
App::TimeBarState App::time_bar_state() const {
    TimeBarState state;
    if (!pairs_ready) return state;
    const int total = pairs_board.level().total_ticks();
    const int left = static_cast<int>(pairs_board.ticks_remaining());
    state.red = left < 900;
    if (timebar.segments > 0 && total > 0) {
        const double per = static_cast<double>(total) / static_cast<double>(timebar.segments);
        if (per > 0.0) state.elapsed = static_cast<int>(static_cast<double>(total - left) / per);
    }
    if (state.elapsed < 0) state.elapsed = 0;
    if (state.elapsed > timebar.segments) state.elapsed = timebar.segments;
    return state;
}

void App::refresh_cards() {
    if (!pairs_ready) return;
    scene_draws.assign(pairs_scene_layer.begin(), pairs_scene_layer.end());
    // The screen's own buttons: a hovered one draws its second <Animation> in place of the idle
    // art scene_draws got from build_scene (the same swap build_menu does for the menu).
    if (hover_button_at >= 0 && hover_button_at < static_cast<int>(conc_hotspots.size())) {
        const Hotspot& spot = conc_hotspots[static_cast<std::size_t>(hover_button_at)];
        if (spot.has_hover && spot.idle_at >= 0 &&
            static_cast<std::size_t>(spot.idle_at) < scene_draws.size()) {
            scene_draws[static_cast<std::size_t>(spot.idle_at)] = spot.hover_draw;
        }
    }
    float extent_w = scene_w, extent_h = scene_h;
    for (std::size_t i = 0; i < card_cache.size(); ++i) {
        CardCache& entry = card_cache[i];
        const bool is_highlight = (i + 1 == card_cache.size());
        if (is_highlight) {
            // The slot is only the outline art the hover below draws.  An earlier session drew
            // highlite_outline here on the card waiting for its partner; the binary says the
            // outline is the focus art and nothing else -- Card+0xD0's visible flag is written
            // only by Card::HandleEvent's FocusGot (0x10007C46F sets it) and FocusLost
            // (0x10007C413 clears it), while UpdateFlipForward / UpdateFlipBackward / OnReveal /
            // OnHide touch only its scale (node+0x1C/+0x20) and position.  In the usual case the
            // two agree, because the card that was just clicked is still under the pointer.
            continue;
        }
        if (entry.index < 0 || entry.index >= pairs_board.cell_count()) continue;
        // The deal-in.  While Board+0x1E8 is 0 the card draws its "IntroImage" -- a copy of the
        // level's card-back art -- growing from 10 percent of its size to 100 with an overshoot,
        // and nothing at all until its `rand() % 60` tick delay has run out.  The card's own art
        // is hidden until Card::StopIntroAnimation (0x10007E390).
        if (pairs_dealing) {
            const int delay = static_cast<int>(entry.index) < static_cast<int>(card_deal_delay.size())
                                  ? card_deal_delay[static_cast<std::size_t>(entry.index)]
                                  : 0;
            const int tick = static_cast<int>(entry.index) < static_cast<int>(card_deal_tick.size())
                                 ? card_deal_tick[static_cast<std::size_t>(entry.index)]
                                 : kCardIntroTicks;
            if (delay > 0) continue;
            if (tick < kCardIntroTicks && entry.tex_back != nullptr) {
                const double v = static_cast<double>(tick) / static_cast<double>(kCardIntroTicks);
                const double u = 1.0 - v;
                const double s = static_cast<double>(pairs_board.slots()[static_cast<std::size_t>(
                                     entry.index)]
                                                         .scale_percent);
                // Card+0x30/10 and Card+0x30/2 are integer divisions in the engine.
                const double f = 6.0 * v * u * u + 0.1 * u * u * u +
                                 (3.0 * std::floor(s / 2.0) / s) * v * v * u + v * v * v;
                const float w = entry.w * static_cast<float>(f);
                const float h = entry.h * static_cast<float>(f);
                DrawItem d;
                d.texture = entry.tex_back;
                d.src = entry.src_back;
                d.dst = SDL_FRect{entry.base_x + (entry.w - w) * 0.5f,
                                  entry.base_y + (entry.h - h) * 0.5f, w, h};
                d.alpha = 255;
                scene_draws.push_back(d);
                extent_w = std::max(extent_w, d.dst.x + d.dst.w);
                extent_h = std::max(extent_h, d.dst.y + d.dst.h);
                continue;
            }
        }
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
    // The hovered card's focus art.  Card::InitUI makes card_glow.xml (Card+0xC8) and
    // highlite_outline.xml (Card+0xD0) children of the card's own Base, so they follow the card
    // through a flip; Card::HandleEvent's FocusGot case (0x10007C46F) shows the outline for any
    // hovered card and the glow only from state 1, a card that is already face up, and
    // Card::OnReveal (0x10007F849) shows the glow for the card that was revealed with the
    // pointer still over it.  The glow is created after FaceAnimation, so it draws over the
    // card's face and under the outline.
    if (hover_card >= 0 && !pairs_dealing &&
        hover_card < static_cast<int>(card_cache.size()) - 1) {
        const std::size_t hc = static_cast<std::size_t>(hover_card);
        const CardCache& entry = card_cache[hc];
        const CardFlip& f = card_flip[hc];
        const pple::ConcentrationSlot& slot = pairs_board.slots()[hc];
        const float sx = static_cast<float>(f.scale_x) / 100.0f;
        const float sy = static_cast<float>(f.scale_y) / 100.0f;
        const float cs = static_cast<float>(slot.scale_percent) / 100.0f;
        // The engine recentres a narrowing card on the cell, not on the sprite, so the highlight
        // rides the same offsets the card's own draw uses.
        const float hx = entry.base_x +
                         (100.0f - static_cast<float>(f.scale_x)) / 100.0f *
                             static_cast<float>(slot.w) * 0.5f;
        const float hy = entry.base_y +
                         fabs(100.0f - static_cast<float>(f.scale_y)) / 100.0f *
                             static_cast<float>(slot.h) * -1.0f;
        if (pairs_glow_tex != nullptr && pairs_board.face(hover_card) == pple::CardFace::Shown) {
            // The glow's own flag is a latch: FocusGot and OnReveal set it and only FocusLost
            // clears it, so the original keeps it on through the take-back flip as long as the
            // pointer stays.  Drawing it from the face state instead drops it one flip earlier.
            DrawItem d;
            d.texture = pairs_glow_tex;
            d.src = pairs_glow_src;
            // Card::InitUI puts the glow at the card's x - 4.
            d.dst = SDL_FRect{hx - 4.0f * cs, hy, pairs_glow_w * sx, pairs_glow_h * sy};
            d.alpha = 255;
            // Card::InitUI sets the glow's material to 2, "additive" (0x10007BCE9, with
            // PurblePlaceUILoader::SetNodeMaterial 0x1000730B0 reading <Material> into
            // node+0x168); the outline keeps the default material.
            d.additive = true;
            scene_draws.push_back(d);
        }
        const CardCache& outline = card_cache.back();
        if (outline.tex_face != nullptr) {
            DrawItem d;
            d.texture = outline.tex_face;
            d.src = outline.src_face;
            d.dst = SDL_FRect{hx, hy, outline.w * sx, outline.h * sy};
            d.alpha = 255;
            scene_draws.push_back(d);
            extent_w = std::max(extent_w, d.dst.x + d.dst.w);
            extent_h = std::max(extent_h, d.dst.y + d.dst.h);
        }
    }
    // The sneak bonus's own mark.  Card::InitUI builds Card+0xA8 ("SneakIcon") from
    // SneakPeakMiniIcon.xml as a child of the card's Base, after card_glow and highlite_outline,
    // so it draws over the card and its focus art; Card::DoSneakIcon (0x10007DE60) shows it while
    // Card+0x18 is set and Card+0x568 < 2, and Board::SelectSneakCard (0x10008B348) sets that
    // flag on one random card.  It is the eye the shipped game puts on some cards.
    if (pairs_sneak_icon_tex != nullptr && !pairs_dealing) {
        for (std::size_t i = 0; i < card_cache.size(); ++i) {
            const CardCache& entry = card_cache[i];
            if (entry.index < 0 || !pairs_board.sneak_card(entry.index)) continue;
            const std::size_t at = static_cast<std::size_t>(entry.index);
            if (at < card_fx.size() && card_fx[at].blank) continue;  // state 2: the card is gone
            const float cs =
                static_cast<float>(pairs_board.slots()[at].scale_percent) / 100.0f;
            DrawItem d;
            d.texture = pairs_sneak_icon_tex;
            d.src = pairs_sneak_icon_src;
            // The icon node sits at the card Base's own origin, so its drawn corner is
            // (x, y) - registration * scale; the registration (-1,-6) is what puts the eye over
            // the card's top-left corner.
            d.dst = SDL_FRect{entry.base_x - pairs_sneak_icon_reg_x * cs,
                              entry.base_y - pairs_sneak_icon_reg_y * cs,
                              pairs_sneak_icon_w * cs, pairs_sneak_icon_h * cs};
            d.alpha = 255;
            scene_draws.push_back(d);
            extent_w = std::max(extent_w, d.dst.x + d.dst.w);
            extent_h = std::max(extent_h, d.dst.y + d.dst.h);
        }
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
        sneak_layer.push_back(i == static_cast<std::size_t>(hover_token) &&
                                      i < pairs_token_hover_draws.size()
                                  ? pairs_token_hover_draws[i]
                                  : pairs_token_draws[i]);
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
    // The time bar: segment i is lit while i >= elapsed, red for the last 900 ticks, and drawn
    // with frame i of its own template ramp (CreateTimeSlice 0x10008C640 stores the slice index
    // in node+0x180), which is what gives the bar its tube profile instead of one flat colour.
    if (timebar.ok) {
        const TimeBarState bar = time_bar_state();
        SDL_Texture* tex = bar.red ? timebar.red : timebar.green;
        const pple::SceneItem& geom = bar.red ? timebar.red_geom : timebar.green_geom;
        const float x = bar.red ? timebar.x_red : timebar.x_green;
        if (tex != nullptr) {
            for (int i = bar.elapsed; i < timebar.segments; ++i) {
                float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
                pple::sprite_frame_rect(geom, i, fx, fy, fw, fh);
                DrawItem d;
                d.texture = tex;
                d.src = SDL_FRect{fx, fy, fw, fh};
                d.dst = SDL_FRect{x, timebar.y_top + static_cast<float>(i), fw, fh};
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
