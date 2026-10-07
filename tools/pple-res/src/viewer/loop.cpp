#include "app.hpp"

namespace pple::viewer {

int App::run_loop() {
    bool running = true;
    // --script-click "x,y;x,y" injects clicks in scene coordinates through the same handler the
    // mouse uses, so menu -> game -> menu and the flip animation can be verified without a mouse.
    // A "wait:MS" entry waits that long before the next click, which is what the Professor's
    // intro needs: his entrance plays for about a second before the first bubble appears.
    struct ScriptedClick {
        bool wait = false;
        float x = 0.0f, y = 0.0f;
        int ms = 0;
    };
    std::vector<ScriptedClick> script_clicks;
    if (!script_click_spec.empty()) {
        std::size_t at = 0;
        while (at < script_click_spec.size()) {
            const std::size_t semi = script_click_spec.find(';', at);
            const std::string item = script_click_spec.substr(at, semi - at);
            ScriptedClick step;
            if (item.rfind("wait:", 0) == 0) {
                step.wait = true;
                step.ms = std::atoi(item.substr(5).c_str());
            } else {
                const std::size_t comma = item.find(',');
                if (comma != std::string::npos) {
                    step.x = static_cast<float>(std::atof(item.substr(0, comma).c_str()));
                    step.y = static_cast<float>(std::atof(item.substr(comma + 1).c_str()));
                }
            }
            script_clicks.push_back(step);
            at = semi == std::string::npos ? script_click_spec.size() : semi + 1;
        }
        std::printf("script: %zu click(s)\n", script_clicks.size());
    }
    std::size_t script_next = 0;
    Uint64 script_last = SDL_GetTicks();
    // --bench-build N: time N whole build_pairs calls with every sheet already cached.  That is
    // the cost the reveal timer used to pay on every one of its 30 Hz ticks.
    if (bench_build > 0 && pairs_ready) {
        for (int i = 0; i < bench_build; ++i) {
            ++seed_for_pairs;
            build_pairs();
        }
        std::printf("bench-build: %zu call(s), %.3f ms total, %.3f ms/call\n", build_calls,
                    build_ms, build_calls == 0 ? 0.0 : build_ms / static_cast<double>(build_calls));
        for (SDL_Texture* t : hud_textures) SDL_DestroyTexture(t);
        for (const auto& [name, t] : sheet_cache) SDL_DestroyTexture(t);
        if (font != nullptr) TTF_CloseFont(font);
        TTF_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
    // --bench: render this many frames as fast as possible and report the card-layer cost.
    std::size_t bench_done = 0;
    const Uint64 bench_start = SDL_GetPerformanceCounter();
    Uint64 last_tick = SDL_GetTicks();
    float pairs_accumulator = 0.0f;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else
            if (event.type == SDL_EVENT_MOUSE_MOTION && screen == Screen::Menu && menu_ready &&
                view_scale > 0.0f) {
                running = click_scene((event.motion.x - view_ox) / view_scale,
                                      (event.motion.y - view_oy) / view_scale, true);
            } else
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT && view_scale > 0.0f) {
                running = click_scene((event.button.x - view_ox) / view_scale,
                                      (event.button.y - view_oy) / view_scale, false);
            } else
            if (event.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = event.key.key;
                if (key == SDLK_ESCAPE || key == SDLK_Q) {
                    // In the game ESC goes back to the menu, as ReturnToMainMenu does; from the
                    // menu it quits.
                    if (screen == Screen::Concentration) {
                        game_to_menu();
                    } else if (screen == Screen::Difficulty) {
                        game_to_menu();
                    } else {
                        running = false;
                    }
                } else
                if ((key == SDLK_R || key == SDLK_N) && pairs_ready) {
                    ++seed_for_pairs;
                    start_game(picker_choice, true);
                } else
                if (!movie_xml.empty() && (key == SDLK_RIGHT || key == SDLK_LEFT || key == SDLK_PAGEDOWN || key == SDLK_PAGEUP)) {
                    const int delta = (key == SDLK_RIGHT || key == SDLK_PAGEDOWN) ? 1 : -1;
                    frame = static_cast<std::size_t>(std::max(0, static_cast<int>(frame) + delta));
                    load_current(true);
                } else
                if (!sprite_xml.empty() && (key == SDLK_RIGHT || key == SDLK_LEFT)) {
                    const std::size_t delta = key == SDLK_RIGHT ? 1 : static_cast<std::size_t>(-1);
                    frame = (frame + delta) % 256;
                    load_current(true);
                } else
                if (screen == Screen::Other && sprite_xml.empty() &&                           (key == SDLK_RIGHT || key == SDLK_DOWN || key == SDLK_PAGEDOWN)) {
                    index = (index + 1) % items.size();
                    load_current(true);
                } else
                if (screen == Screen::Other && sprite_xml.empty() &&                           (key == SDLK_LEFT || key == SDLK_UP || key == SDLK_PAGEUP)) {
                    index = (index + items.size() - 1) % items.size();
                    load_current(true);
                } else
                if (key == SDLK_SPACE) {
                    integer_scale = !integer_scale;
                } else
                if (key == SDLK_S && current.ok) {
                    std::string name = sprite_xml.empty() ? items[index].slot.entry->name : sprite_xml;
                    std::replace(name.begin(), name.end(), '\\', '_');
                    const std::string out = name + ".ppm";
                    std::printf("saving %s: %s\n", out.c_str(), write_ppm(out, current) ? "ok" : "failed");
                }
            }
        }
        int out_w = 0, out_h = 0;
        SDL_GetRenderOutputSize(renderer, &out_w, &out_h);
        // A click that advanced the level or let the Mixer reshuffle re-deals the art here, once
        // the click handler has returned.
        if (pairs_rebuild_pending && pairs_ready) {
            pairs_rebuild_pending = false;
            build_pairs();
        }
        // --loading MS: the loading screen is up for MS milliseconds, which stands in for however
        // long PurblePlaceManager::Init takes in the shipped game, then the menu arrives.
        if (screen == Screen::Loading && SDL_GetTicks() - loading_start >= static_cast<Uint64>(loading_ms)) {
            if (menu_mode) {
                screen = Screen::Menu;
                build_menu();
            }
            // Without --menu the loading screen stays up so --shot can capture it.
        }
        // The difficulty chooser's own loop: the Professor waits in the classroom while the
        // player decides, so the screen is only redrawn when his 12 fps movie moves on.
        if (screen == Screen::Difficulty) {
            const Uint64 now = SDL_GetTicks();
            picker_time += static_cast<float>(now - picker_last_tick) / 1000.0f;
            picker_last_tick = now;
            const int frame = static_cast<int>(picker_time * 12.0f);
            if (frame != picker_frame) {
                picker_frame = frame;
                draw_difficulty();
            }
        }
        // Purble Pairs runs on the engine's fixed 30 Hz step.
        if (pairs_ready) {
            const Uint64 now = SDL_GetTicks();
            const float elapsed = static_cast<float>(now - last_tick) / 1000.0f;
            last_tick = now;
            pairs_accumulator += elapsed;
            bool ticked = false;
            bool flipped = false;
            while (pairs_accumulator >= (1.0f / 30.0f)) {
                pairs_accumulator -= (1.0f / 30.0f);
                // The card deal-in: every card's IntroImage grows for 45 ticks after its own
                // delay, and Board::Update only makes the board playable once the last one has
                // arrived.
                if (advance_deal()) flipped = true;
                const int before = pairs_board.pending_reveal();
                const int faces_before = pairs_board.face_revision();
                const bool sneak_before = pairs_board.sneak_active();
                const TimeBarState bar_before = time_bar_state();
                pairs_board.tick();
                const TimeBarState bar_after = time_bar_state();
                // Each mismatched pair has its own countdown now, so the faces themselves are what
                // says whether a tick turned something over.  The time bar's segment and its red
                // threshold are part of the same draw list, so a clock tick that moves them counts
                // too -- otherwise only a mismatch would keep the bar up to date.
                if (pairs_board.pending_reveal() != before ||
                    pairs_board.face_revision() != faces_before ||
                    pairs_board.sneak_active() != sneak_before ||
                    bar_after.elapsed != bar_before.elapsed || bar_after.red != bar_before.red) {
                    ticked = true;
                    sync_flips();
                }
                // The flip runs on the same 30 Hz step as the engine's Card::Update, and so does
                // the match animation.
                if (advance_flips()) flipped = true;
                if (advance_card_fx()) flipped = true;
                // Professor::Update (0x10008C088): the movie clock, the entrance -> tip
                // transition, the tip's own timer and the win/lose exit.
                professor_advance();
                if (pairs_sneak_movie_ok && pairs_sneak_frame >= 0.0f) {
                    const float fps = pairs_sneak_movie.parts.empty() ? pairs_sneak_movie.fps
                        : pple::movie_frame_rate(pairs_sneak_movie, pairs_sneak_movie.parts.front());
                    pairs_sneak_frame += fps / 30.0f;
                    if (pairs_sneak_frame >= static_cast<float>(pairs_sneak_movie.frame_count))
                        pairs_sneak_frame = -1.0f;
                    flipped = true;
                }
                // The jar (Jar::Update 0x10008373C).  The coins fly for /ArcAnimation/Frames
                // ticks; when the array empties and the board is complete the burst starts; when
                // the burst's movie runs out, Jar::StopExplosion ends the board.
                if (pairs_jar_phase == JarPhase::Flying) {
                    bool landed = false;
                    for (std::size_t i = 0; i < pairs_arcs.size();) {
                        if (++pairs_arcs[i].tick >= pairs_arc_frames) {
                            pairs_arcs.erase(pairs_arcs.begin() + static_cast<std::ptrdiff_t>(i));
                            landed = true;
                        } else {
                            ++i;
                        }
                    }
                    flipped = true;  // the coins moved, so the frame has to be rebuilt
                    if (landed) jar_grow();  // ArcBehavior::Update's tail calls Jar::DoFiller
                    if (pairs_arcs.empty()) {
                        pairs_jar_phase = JarPhase::Idle;
                        jar_begin_burst();
                    }
                }
                if (pairs_jar_phase == JarPhase::Burst) {
                    // The engine's default is 30 fps when the library carries no FPS attribute,
                    // and one tick is 1/30 s, so the movie advances one frame per tick.
                    const BurstMovie* burst = load_burst(pairs_burst_name);
                    const int frames = burst != nullptr ? burst->movie.frame_count : 0;
                    pairs_burst_frame += 1.0f;
                    flipped = true;
                    if (frames <= 0 || pairs_burst_frame >= static_cast<float>(frames))
                        finish_board_end();
                }
                // A board that completed with nothing left to fly -- the coin sprite did not
                // resolve, or a scripted hook ended it -- still runs the burst.
                if (pairs_board.end_pending() && pairs_jar_phase == JarPhase::Idle) {
                    jar_begin_burst();
                    if (pairs_jar_phase == JarPhase::Idle) finish_board_end();
                }
                // End of round.  Board::Update's loss goes straight to OnEnterState(3), and
                // OnEnterWonGameState is reached from Jar::StopExplosion, so `round_over` is the
                // signal and `won()` alone is not.
                report_round_end();
            }
            report_round_end();  // a scripted hook can end the round without a tick
            if (ticked || flipped) {
                timed_refresh();
                refresh_hud();
            }
        } else
        {
            last_tick = SDL_GetTicks();
        }
        if (bench_frames > 0) {
            ++bench_done;
            if (bench_done >= static_cast<std::size_t>(bench_frames)) {
                const Uint64 t1 = SDL_GetPerformanceCounter();
                const double total_ms = 1000.0 * static_cast<double>(t1 - bench_start) /
                                        static_cast<double>(SDL_GetPerformanceFrequency());
                std::printf("bench: %zu frames in %.1f ms (%.3f ms/frame)\n", bench_done, total_ms,
                            total_ms / static_cast<double>(bench_done));
                std::printf("bench: card layer %zu call(s), %.4f ms total, %.4f ms/call, "
                            "%.4f ms/call primed (first call %.4f ms)\n",
                            refresh_calls, refresh_total_ms,
                            refresh_calls == 0 ? 0.0
                                               : refresh_total_ms / static_cast<double>(refresh_calls),
                            refresh_calls <= 1 ? 0.0
                                               : (refresh_total_ms - refresh_first_ms) /
                                                     static_cast<double>(refresh_calls - 1),
                            refresh_first_ms);
                std::printf("bench: full build_pairs %zu call(s), %.3f ms total, %.3f ms/call\n",
                            build_calls, build_ms,
                            build_calls == 0 ? 0.0 : build_ms / static_cast<double>(build_calls));
                running = false;
            }
            if (!offscreen) SDL_Delay(0);
        }
        SDL_SetRenderDrawColor(renderer, 24, 24, 32, 255);
        SDL_RenderClear(renderer);
        // Reserve the top strip for the HUD.
        const int hud_strip = hud_textures.empty() ? 0 : hud_h + 12;
        // The difficulty chooser is a screen of its own; everything else draws the scene and
        // then whatever overlays it (the tip bubble and the end-of-round panel).
        const bool picker_screen = screen == Screen::Difficulty;
        const std::vector<DrawItem>& base_draws = picker_screen ? picker_draws : scene_draws;
        const float base_w = picker_screen ? picker_w : scene_w;
        const float base_h = picker_screen ? picker_h : scene_h;
        if (!base_draws.empty() || !overlay_draws.empty()) {
            const float area_w = static_cast<float>(out_w);
            const float area_h = static_cast<float>(out_h - hud_strip);
            const float sx = base_w > 0.0f ? area_w / base_w : 1.0f;
            const float sy = base_h > 0.0f ? area_h / base_h : 1.0f;
            float s = std::min(sx, sy);
            // The scene is 800x496.  Flooring the ratio pinned a 1400x900 window to scale 1, so
            // the board was drawn at its native size inside a window more than three times its
            // area.  Fill the window by default and keep SPACE for pixel-exact integer scaling.
            if (integer_scale && s > 1.0f) s = std::floor(s);
            if (s <= 0.0f) s = 1.0f;
            const float ox = (area_w - base_w * s) * 0.5f;
            const float oy = static_cast<float>(hud_strip) + (area_h - base_h * s) * 0.5f;
            view_scale = s;
            view_ox = ox;
            view_oy = oy;
            // One scripted entry per frame, spaced so a flip or a rebuild can settle; a "wait:MS"
            // entry only moves the clock the spacing is measured from.
            if (script_next < script_clicks.size()) {
                const Uint64 now = SDL_GetTicks();
                if (script_next == 0 || now - script_last >= 150) {
                    script_last = now;
                    const ScriptedClick step = script_clicks[script_next++];
                    if (step.wait) {
                        SDL_Delay(static_cast<Uint32>(std::max(0, step.ms) - 150));
                    } else {
                        std::printf("script click %zu: scene %.0f,%.0f\n", script_next - 1,
                                    static_cast<double>(step.x), static_cast<double>(step.y));
                        running = click_scene(step.x, step.y, false);
                    }
                }
            }
            const auto draw_item = [&](const DrawItem& d) {
                if (d.texture == nullptr) return;
                SDL_SetTextureAlphaMod(d.texture, static_cast<Uint8>(std::clamp(d.alpha, 0, 255)));
                const SDL_FRect dst{
                    ox + d.dst.x * s, oy + d.dst.y * s, d.dst.w * s, d.dst.h * s
                };
                const SDL_FRect* src = (d.src.w > 0.0f && d.src.h > 0.0f) ? &d.src : nullptr;
                if (d.rotation != 0.0f) {
                    SDL_RenderTextureRotated(renderer, d.texture, src, &dst, static_cast<double>(d.rotation),                                             nullptr, SDL_FLIP_NONE);
                } else
                {
                    SDL_RenderTexture(renderer, d.texture, src, &dst);
                }
                SDL_SetTextureAlphaMod(d.texture, 255);
            };
            // The menu's cloud movie sits between the Background and the Foreground, which is
            // where PurblePlaceManager::Init's own container puts it: it is created as a child of
            // the Background sprite, so it draws after the background and before the next sibling.
            const auto draw_clouds = [&]() {
                if (!menu_clouds_ok || menu_clouds.parts.empty()) return;
                const int frames = std::max(1, menu_clouds.frame_count);
                const double seconds = static_cast<double>(SDL_GetTicks() - menu_cloud_start) / 1000.0;
                int cf = static_cast<int>(seconds * static_cast<double>(menu_cloud_fps));
                // The engine turns the movie's loop off (0x10007131E), so it plays once and rests
                // on its last frame.
                if (cf >= frames) cf = frames - 1;
                std::vector<pple::MoviePlacement> placements;
                pple::movie_evaluate(menu_clouds, cf, placements);
                for (const pple::MoviePlacement& p : placements) {
                    if (p.part == nullptr || !p.part->resolved || p.part->sprite.sheet.empty())
                        continue;
                    std::string sheet_how;
                    const auto* sheet_slot = bundle.resolve(p.part->sprite.sheet, sheet_how);
                    if (sheet_slot == nullptr) continue;
                    std::string note;
                    SDL_Texture* tex = load_sheet_texture(p.part->sprite.sheet,
                                                          sheet_slot->entry->name, note);
                    if (tex == nullptr) continue;
                    float sx2 = 0.0f, sy2 = 0.0f, sw2 = 0.0f, sh2 = 0.0f;
                    pple::sprite_frame_rect(p.part->sprite, 0, sx2, sy2, sw2, sh2);
                    DrawItem d;
                    d.texture = tex;
                    d.src = SDL_FRect{sx2, sy2, sw2, sh2};
                    d.dst = SDL_FRect{menu_clouds_x + p.x - p.part->sprite.reg_x,
                                      menu_clouds_y + p.y - p.part->sprite.reg_y, sw2 * p.xscale,
                                      sh2 * p.yscale};
                    d.alpha = static_cast<int>(std::clamp(p.alpha * 255.0f, 0.0f, 255.0f));
                    d.rotation = p.rotation;
                    draw_item(d);
                }
            };
            const auto draw_all = [&](const std::vector<DrawItem>& list, bool clouds) {
                for (std::size_t i = 0; i < list.size(); ++i) {
                    if (clouds && i == menu_cloud_at) draw_clouds();
                    draw_item(list[i]);
                }
                if (clouds && menu_cloud_at >= list.size()) draw_clouds();
            };
            // The menu's clouds only belong on the menu screen; the chooser and the board have
            // no cloud slot.
            const bool clouds_here = picker_screen ? false : screen == Screen::Menu;
            draw_all(base_draws, clouds_here);
            // The tip bubble and the end-of-round panel sit over the live board.
            for (const DrawItem& d : overlay_draws) draw_item(d);
        }
        if (base_draws.empty() && overlay_draws.empty() && texture != nullptr) {
            const SDL_FRect fitted =                fit_rect_within(out_w, out_h - hud_strip, static_cast<int>(src_rect.w),                                static_cast<int>(src_rect.h), integer_scale);
            const SDL_FRect dst{
                fitted.x, fitted.y + hud_strip, fitted.w, fitted.h
            };
            SDL_RenderTexture(renderer, texture, &src_rect, &dst);
        }
        if (!hud_textures.empty()) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
            const SDL_FRect panel{
                0.0f, 0.0f, static_cast<float>(out_w), static_cast<float>(hud_strip)
            };
            SDL_RenderFillRect(renderer, &panel);
            float y = 6.0f;
            for (SDL_Texture* t : hud_textures) {
                float tw = 0.0f, th = 0.0f;
                SDL_GetTextureSize(t, &tw, &th);
                const SDL_FRect where{
                    6.0f, y, tw, th
                };
                SDL_RenderTexture(renderer, t, nullptr, &where);
                y += th;
            }
        }
        if (offscreen) {
            if (script_next < script_clicks.size()) {
                // Still working through --script-click: keep rendering, capture on the last one.
                SDL_RenderPresent(renderer);
                SDL_Delay(16);
                continue;
            }
            // --shot-delay lets a flip or an animation reach a known point before the capture.
            if (shot_delay_ms > 0 && SDL_GetTicks() - script_last < static_cast<Uint64>(shot_delay_ms)) {
                SDL_RenderPresent(renderer);
                SDL_Delay(8);
                continue;
            }
            const bool ok = capture(renderer, shot_path);
            SDL_RenderPresent(renderer);
            running = false;
            if (!ok) {
                SDL_DestroyTexture(texture);
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 1;
            }
        } else
        {
            SDL_RenderPresent(renderer);
            SDL_Delay(8);
        }
    }
    for (SDL_Texture* t : hud_textures) SDL_DestroyTexture(t);
    if (texture != nullptr) SDL_DestroyTexture(texture);
    release_overlay();
    free_direct(picker_draws);
    for (const auto& [name, t] : sheet_cache) SDL_DestroyTexture(t);
    if (font != nullptr) TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace pple::viewer
