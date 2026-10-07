#include "app.hpp"
#include "audio.hpp"

namespace pple::viewer {

void App::build_pairs() {
    const Uint64 build_t0 = SDL_GetPerformanceCounter();
    std::string scene_how;
    const auto* scene_slot = bundle.resolve(scene_xml, scene_how);
    const auto* cards_slot = bundle.find("UI\\CONCENTRATION\\CARDS.XML");
    if (scene_slot == nullptr || cards_slot == nullptr) {
        std::fprintf(stderr, "pairs: need UI\\CONCENTRATION\\CONCENTRATIONGAME.XML and CARDS.XML\n");
        return;
    }
    const std::vector<std::uint8_t> scene_bytes = bundle.read(*scene_slot);
    std::string error;
    if (!pple::concentration_pools_parse(bundle.read(*cards_slot), pairs_pools, error)) {
        std::fprintf(stderr, "CARDS.XML: %s\n", error.c_str());
        return;
    }
    pple::ConcentrationConstants constants;
    std::vector<pple::ConcentrationLevel> levels;
    if (!pple::concentration_game_parse(scene_bytes, constants, levels, error)) {
        std::fprintf(stderr, "CONCENTRATIONGAME.XML: %s\n", error.c_str());
        return;
    }
    pple::concentration_cards_box_parse(scene_bytes, pairs_box, error);
    pairs_constants = constants;
    // RANKS.XML is read by ConRoundData::CalcEndGame at the end of a won round.
    if (!pairs_ranks_ok) {
        const auto* ranks_slot = bundle.find("UI\\CONCENTRATION\\RANKS.XML");
        if (ranks_slot != nullptr) {
            std::string ranks_error;
            pairs_ranks_ok = pple::concentration_ranks_parse(bundle.read(*ranks_slot), pairs_ranks,
                                                             ranks_error);
            if (!pairs_ranks_ok)
                std::printf("  ranks: RANKS.XML did not parse (%s)\n", ranks_error.c_str());
        } else {
            std::printf("  ranks: UI\\Concentration\\Ranks.xml not found\n");
        }
    }
    // --pairs "Difficulty,Level[,Seed]"
    std::string want_difficulty = "Beginner";
    int want_level = 0;
    {
        std::vector<std::string> parts;
        std::size_t at = 0;
        while (at <= pairs_spec.size()) {
            const std::size_t comma = pairs_spec.find(',', at);
            parts.push_back(pairs_spec.substr(at, comma - at));
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        if (!parts.empty() && !parts[0].empty()) want_difficulty = parts[0];
        if (parts.size() > 1 && !parts[1].empty()) want_level = std::atoi(parts[1].c_str());
        if (parts.size() > 2 && !parts[2].empty()) seed_for_pairs = std::stoul(parts[2]);
    }
    const pple::ConcentrationLevel* chosen = nullptr;
    // A rebuild after the Mixer reshuffled the cards or after Board::NewLevel advanced the
    // level must NOT re-deal: the board already carries the new state.  The scene, the
    // Professor and the art are rebuilt, the board is left alone.
    const bool keep_state = pairs_keep_state;
    pairs_keep_state = false;
    if (keep_state && pairs_ready)
        chosen = &pairs_board.level();
    else
        for (const pple::ConcentrationLevel& l : levels)            if (l.difficulty == want_difficulty && l.level_index == want_level) chosen = &l;
    if (chosen == nullptr && !levels.empty()) chosen = &levels.front();
    if (chosen == nullptr) {
        std::fprintf(stderr, "pairs: no level\n");
        return;
    }
    // <TimeBarGreen>/<TimeBarRed> are segment templates, and the bar background is hidden on
    // an untimed difficulty (TimeBar::InitUI 0x10008538E tests difficulty 0 or Board+0x154).
    hidden_instances.clear();
    hidden_instances.insert("TimeBarGreen");
    hidden_instances.insert("TimeBarRed");
    hidden_instances.insert("SneakPeekToken");  // dynamic visibility follows Round.Tokens
    // InitUI (0x1000849C0) never instantiates the XML's TokenAnimate template;
    // its SneakSparks sprites are explicitly hidden at 0x100084CE5.
    hidden_instances.insert("SneakPeekTokenAnimate");
    hidden_instances.insert("SneakSparks");
    if (!chosen->timed()) hidden_instances.insert("TimeBarBkg");
    scene_background_xml = "UI\\Concentration\\hearts.xml";
    build_scene();
    // Board::Update (0x10008A4F0) drives the end of the round and
    // ConcentrationGame::OnEnterWonGameState (0x1000821D0) / OnEnterLoseGameState
    // (0x10008259C) start the Professor's win and lose movies; OnWinAnimStop
    // (0x100082670) / OnLoseAnimStop (0x10008272C) end them.  The idle movie runs in between.
    professor = ProfessorDraw{};
    if (loaded_scene) {
        for (const pple::SceneItem& item : loaded_scene->containers) {
            const std::string& key = item.name.empty() ? item.instance : item.name;
            if (key == "ProfessorContainer") {
                professor.anchor_x = item.x;
                professor.anchor_y = item.y;
            }
        }
    }
    if (!load_professor("ProfessorIdleAnimation"))
        std::printf("  professor: ProfessorIdleAnimation did not load\n");
    else
        std::printf("  professor: %d frame(s), %zu part(s), anchor %.0f,%.0f\n",
                    professor.movie.frame_count, professor.movie.parts.size(),
                    static_cast<double>(professor.anchor_x),
                    static_cast<double>(professor.anchor_y));
    // Jar::StopExplosion (0x100083A20) queues ProIntLevelChange / ProAdvLevelChange and calls
    // Professor::MoveIn before Board::NewLevel re-deals, so the tip outlives this rebuild and
    // has to be shown again once the new board's art exists.
    if (!pending_level_tip.empty()) {
        const std::string tip_id = pending_level_tip;
        pending_level_tip.clear();
        professor_level_tip(tip_id);
    }
    initialise_pair_audio(bundle, mute);
    if (keep_state) {
        // The board keeps its slot layout, deck, score, level and tick budget.
    } else {
        // The board needs the difficulty's whole <Levels> list: Board::NewLevel walks it, so
        // Intermediate plays two boards and Advanced four.
        std::vector<pple::ConcentrationLevel> chosen_levels;
        std::size_t chosen_at = 0;
        for (const pple::ConcentrationLevel& l : levels) {
            if (l.difficulty != chosen->difficulty) continue;
            if (l.level_index == chosen->level_index) chosen_at = chosen_levels.size();
            chosen_levels.push_back(l);
        }
        if (!pairs_board.reset(chosen_levels, chosen_at, pairs_pools, constants, pairs_box,
                               seed_for_pairs)) {
            std::fprintf(stderr, "pairs: board reset failed\n");
            return;
        }
    }
    if (!keep_state && pairs_reveal) {
        // Reveal the whole deck, for inspecting the card art of every level.
        pairs_board.reveal_all();
    }
    // --pairs-solve clicks every matchable pair through the same handler, so the win state,
    // the score and the Professor's win movie can be reached without a mouse.  The rule is
    // Card::DoMatcher's: equal <id>, or either card is the Wild Card.  The wild is matched
    // last, because spending it on the first partner that happens to be available strands
    // the deck's single Time Bonus / Chef's cake cards.
    if (!keep_state && pairs_solve) {
        const int wild = pairs_pools.wild;
        const auto id_of = [&](int i) { return pairs_board.card_id(i); };
        const auto hidden = [&](int i) {
            return pairs_board.face(i) == pple::CardFace::Hidden;
        };
        const auto run_phase = [&](int phase) {
            bool progress = true;
            int guard = 0;
            while (progress && !pairs_board.finished() && guard++ < 128) {
                progress = false;
                for (int i = 0; i < pairs_board.cell_count() && !pairs_board.finished(); ++i) {
                    if (!hidden(i)) continue;
                    const int a = id_of(i);
                    for (int j = i + 1; j < pairs_board.cell_count(); ++j) {
                        if (!hidden(j)) continue;
                        const int b = id_of(j);
                        const bool wild_a = a == wild, wild_b = b == wild;
                        const bool take = phase == 0 ? (a == b && !wild_a && !wild_b)
                                         : phase == 1 ? (wild_a != wild_b)
                                                      : (wild_a && wild_b);
                        if (!take) continue;
                        if (!pairs_board.click(i).accepted) break;
                        const pple::ClickResult r = pairs_board.click(j);
                        if (!r.accepted) break;
                        std::printf("  solve click %d,%d -> %s, score %d, matches %d/%d\n", i, j,
                                    r.match ? "MATCH"
                                            : (r.wild_match ? "WILD (+10)"
                                                            : (r.mismatch ? "MISS" : "revealed")),
                                    pairs_board.score(), pairs_board.matches(),
                                    pairs_board.round_pairs_total());
                        // The special cards, so each one's effect is visible in the output
                        // rather than only in the code.
                        if (r.chef)
                            std::printf("    chef cake: swept every card with the Chef id\n");
                        if (r.mixer)
                            std::printf("    mixer: Board::DoMixer reshuffled the cards left\n");
                        if (r.sneak_get)
                            std::printf("    sneak icon: +%d (SneakMatchBonus)\n",
                                        pairs_constants.sneak_match_bonus);
                        if (r.time_bonus > 0)
                            std::printf("    time bonus: +%d ticks, %.0f s left\n", r.time_bonus,
                                        static_cast<double>(pairs_board.seconds_remaining()));
                        // Jar::StopExplosion runs at the end of the jar's burst, which this
                        // headless path has no frames for: end the board here so the solve
                        // walks every <Level> the way the engine does.  The card animation is
                        // taken straight to Card::OnHide's end state for the same reason.
                        arm_match_fx(r.flew, true);
                        finish_board_end();
                        progress = true;
                        break;
                    }
                }
            }
        };
        for (int phase = 0; phase < 3 && !pairs_board.finished(); ++phase) run_phase(phase);
        std::printf("  solve: %s, score %d, matches %d/%d\n",
                    pairs_board.won() ? "WON" : (pairs_board.lost() ? "LOST" : "unfinished"),
                    pairs_board.score(), pairs_board.matches(), pairs_board.round_pairs_total());
        if (!pairs_board.won()) {
            for (int i = 0; i < pairs_board.cell_count(); ++i) {
                if (pairs_board.face(i) == pple::CardFace::Matched) continue;
                std::printf("    left: cell %d id %d face %d (wild %d)\n", i, id_of(i),
                            static_cast<int>(pairs_board.face(i)), wild);
            }
            std::printf("    pending reveal %d, mismatch timer %d\n", pairs_board.pending_reveal(),
                        pairs_board.mismatch_ticks());
        }
    }
    // --pairs-ticks N advances the engine's 30 Hz step N times before the first frame, so the
    // time bar, the red threshold and the loss state can be inspected without waiting.
    if (!keep_state && pairs_ticks > 0) {
        pairs_board.tick(pairs_ticks);
        std::printf("  %d tick(s): %.0f s left, %s\n", pairs_ticks,
                    static_cast<double>(pairs_board.seconds_remaining()),
                    pairs_board.lost() ? "LOST" : (pairs_board.won() ? "WON" : "running"));
    }
    pairs_ready = true;
    pairs_end_played = false;
    // Board::InitUI (0x10008934D) plays Con_NewScreen (proto 0x71) whenever it builds a new
    // board, which is once per round and once per level change.
    if (!keep_state) play_pair_sfx("Con_NewScreen");
    // --pairs-click 3,7,12 flips those cells through the same handler the mouse uses, so a
    // scripted run exercises the reveal, the match, the scoring and the sounds.
    if (!keep_state && !pairs_click.empty()) {
        std::size_t at = 0;
        while (at < pairs_click.size()) {
            const std::size_t comma = pairs_click.find(',', at);
            const int cell = std::atoi(pairs_click.substr(at, comma - at).c_str());
            at = comma == std::string::npos ? pairs_click.size() : comma + 1;
            if (cell < 0 || cell >= pairs_board.cell_count()) {
                std::printf("  click %d: out of range\n", cell);
                continue;
            }
            if (pairs_board.face(cell) != pple::CardFace::Hidden) {
                // A face-up card goes back down through the same handler (Card::HandleEvent).
                pairs_board.click(cell);
                std::printf("  click %-3d -> turned back down\n", cell);
                sync_flips();
                continue;
            }
            const pple::ClickResult r = pairs_board.click(cell);
            if (!r.accepted) {
                std::printf("  click %d: refused (mismatch timer %d)\n", cell,
                            pairs_board.mismatch_ticks());
                continue;
            }
            std::printf("  click %-3d -> %s, score %d, matches %d/%d%s\n", cell,
                        r.match ? "MATCH" : (r.wild_match ? "WILD" : (r.mismatch ? "MISS" : "revealed")),
                        pairs_board.score(), pairs_board.matches(),
                        pairs_board.round_pairs_total(),
                        pairs_board.mismatch_ticks() >= 0 ? ", reveal timer armed" : "");
            play_click_sfx(r);
            // The scripted path has no frames to animate in, so the card animation is taken
            // to its end state and the jar's flight and burst are skipped: Jar::StopExplosion
            // is called straight away.
            arm_match_fx(r.flew, true);
            if (pairs_board.end_pending()) {
                jar_begin_burst();
                finish_board_end();
            }
            if (r.relaid_out) {
                // The rest of the script refers to the old board, so stop here and let the
                // main loop rebuild the art.
                pairs_rebuild_pending = true;
                pairs_keep_state = true;
                break;
            }
        }
        // let the queued sounds actually reach the device before the capture or exit
        SDL_Delay(600);
    }
    build_pair_level_art(scene_bytes, scene_how);
    const int failed = build_pair_card_assets();
    // The board the focus was on is gone -- a new difficulty has a different cell count -- so the
    // hover starts empty and the next mouse move sets it again.
    hover_card = -1;
    hover_token = -1;
    hover_button_at = -1;
    // Card::InitUI (0x10007C0B6) puts every card in state 6 and clears Board+0x1E8, so the board
    // is inert while the cards grow in; Board::Update then calls Professor::DisplayIntro.  The
    // headless hooks take Card::InitUI's other branch -- Card+0x14 = 45, Board+0x1E8 = 1, no
    // IntroImage -- because a scripted run has no frames to watch the deal with, which is the
    // same reason --pairs-click and --pairs-solve skip the jar's flight.  A rebuild of an
    // already-dealt board (Board::NewLevel or the Mixer) does not deal again.
    const bool deal_this_board = !keep_state && deal_enabled && pairs_click.empty() &&
                                 !pairs_solve && !pairs_reveal && pairs_ticks <= 0;
    if (deal_this_board)
        start_deal();
    else
        finish_deal();
    pairs_scene_draws = scene_draws.size();
    pairs_scene_layer = scene_draws;
    pairs_scene_width = pple::kScreenWidth;
    pairs_scene_height = pple::kScreenHeight + (mce_mode ? 51.0f : 0.0f);
    build_pair_hud(scene_how);
    // Clickable elements of the game screen.  ReturnToMainMenu is a real node; the MCE
    // New Game / Exit Game pair are drawn only with --mce (see ConcentrationGame::LoadGameUI
    // 0x10008122B).  Their rect is the engine's: (x, y) - registration, sized SourceWidth x
    // SourceHeight.
    conc_hotspots.clear();
    if (loaded_scene) {
        for (const pple::SceneItem& item : loaded_scene->items) {
            const std::string& key = item.name.empty() ? item.instance : item.name;
            const bool wanted = key == "ReturnToMainMenu" ||
                                (mce_mode && (key == "NewGame" || key == "ExitGame"));
            if (!wanted || item.sheet.empty()) continue;
            Hotspot spot;
            spot.name = key;
            spot.rect = SDL_FRect{(item.x - item.reg_x * item.scale) * item.scale,
                                  (item.y - item.reg_y * item.scale) * item.scale,
                                  item.src_w * item.scale, item.src_h * item.scale};
            // The button's second <Animation> is its hover art (NodeButton::HandleEvent
            // 0x1000AAC92).  ReturnToMainMenu.xml keeps the idle 95x52 in the Tips sheet at
            // 97,180 and both the hover and pressed pairs at 0,0 and 0,108, so the swap has to
            // replace the idle draw instead of covering it.
            const auto idle = scene_button_at.find(key);
            if (idle != scene_button_at.end()) spot.idle_at = idle->second;
            const Sprite hover = resolve_sprite(bundle, item.ref, 1);
            if (hover.ok && spot.idle_at >= 0) {
                std::string hover_how;
                const auto* hover_slot = bundle.resolve(hover.requested, hover_how);
                if (hover_slot != nullptr) {
                    SDL_Texture* hover_tex =
                        load_sheet_texture(hover.requested, hover_slot->entry->name, pairs_sheet_note);
                    if (hover_tex != nullptr) {
                        spot.hover_draw.texture = hover_tex;
                        spot.hover_draw.src = SDL_FRect{hover.src.x + (hover.prepped ? 1.0f : 0.0f),
                                                        hover.src.y + (hover.prepped ? 1.0f : 0.0f),
                                                        hover.src.w, hover.src.h};
                        spot.hover_draw.dst = SDL_FRect{
                            item.x - item.reg_x * item.scale,
                            item.y - item.reg_y * item.scale + (mce_mode ? 51.0f : 0.0f),
                            hover.src.w * item.scale, hover.src.h * item.scale};
                        spot.hover_draw.alpha = item.alpha;
                        spot.has_hover = true;
                    }
                }
            }
            conc_hotspots.push_back(spot);
        }
    }
    // The two MCE header buttons sit at y=-51/-41 in the document, i.e. above the scene
    // origin; they only land on screen if the host draws the scene 51 px down inside a
    // header strip, which is what --mce reproduces.
    if (mce_mode) {
        for (Hotspot& spot : conc_hotspots)
            if (spot.name == "NewGame" || spot.name == "ExitGame") spot.rect.y += 51.0f;
    }
    pairs_title = scene_xml + "   [Purble Pairs] " + chosen->difficulty + " #" +
                  std::to_string(chosen->level_index);
    std::printf("pairs %s #%d: %d cells, cell %dx%d scale %d%%, %s\n",                    chosen->difficulty.c_str(), chosen->level_index, pairs_board.cell_count(),                    pairs_board.slots().front().w, pairs_board.slots().front().h,                    pairs_board.slots().front().scale_percent,                    (timebar.ok ? (std::to_string(timebar.segments) + " time bar segments")                                : std::string("untimed")).c_str());
    if (failed != 0) std::printf("  %d card sprite(s) did not resolve\n", failed);
    refresh_hud();
    timed_refresh();
    const Uint64 build_t1 = SDL_GetPerformanceCounter();
    build_ms += 1000.0 * static_cast<double>(build_t1 - build_t0) /
                static_cast<double>(SDL_GetPerformanceFrequency());
    ++build_calls;
}

} // namespace pple::viewer
