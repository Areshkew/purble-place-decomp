#include "app.hpp"
#include "audio.hpp"

namespace pple::viewer {

// The engine's focus follows the mouse: UserInterface::ProcessMouseMove (0x100098498) hit-tests
// the node under the cursor and calls SetFocus (0x1000992E0), so exactly one node at a time is
// focused and only that node draws its focus art.  The order here is click_scene's own -- the
// screen's buttons, then the cards topmost-first, then the sneak tokens -- and a hit test that
// finds nothing clears every one of them, which is what a mouse leaving the board does.
void App::update_hover(float sx, float sy) {
    int button_at = -1, card = -1, token = -1;
    for (std::size_t i = 0; i < conc_hotspots.size(); ++i) {
        const SDL_FRect& r = conc_hotspots[i].rect;
        if (sx >= r.x && sx < r.x + r.w && sy >= r.y && sy < r.y + r.h) {
            button_at = static_cast<int>(i);
            break;
        }
    }
    if (button_at < 0) {
        for (std::size_t c = card_cache.size(); c-- > 0;) {
            const CardCache& entry = card_cache[c];
            if (entry.index < 0) continue;  // the highlight slot itself
            if (sx >= entry.base_x && sx < entry.base_x + entry.w &&
                sy >= entry.base_y && sy < entry.base_y + entry.h) {
                card = static_cast<int>(c);
                break;
            }
        }
    }
    if (button_at < 0 && card < 0) {
        for (std::size_t t = 0; t < pairs_tokens.size(); ++t) {
            if (t >= static_cast<std::size_t>(pairs_board.tokens())) break;
            const SDL_FRect& r = pairs_tokens[t];
            if (sx >= r.x && sx < r.x + r.w && sy >= r.y && sy < r.y + r.h) {
                token = static_cast<int>(t);
                break;
            }
        }
    }
    if (button_at == hover_button_at && card == hover_card && token == hover_token) return;
    hover_button_at = button_at;
    hover_card = card;
    hover_token = token;
    std::string what;
    if (card >= 0) what = "card " + std::to_string(card);
    if (token >= 0) what += (what.empty() ? "" : ", ") + ("sneak token " + std::to_string(token));
    if (button_at >= 0)
        what += (what.empty() ? "" : ", ") +
                conc_hotspots[static_cast<std::size_t>(button_at)].name;
    std::printf("  hover: %s\n", what.empty() ? "nothing" : what.c_str());
    // The old target's art has to come off and the new one's has to go on, so both changes
    // rebuild the same layer: the scene layer holds the buttons, the cards and the tokens.
    // Two of those targets also own a simple tip (the sneak token's "SneakPeak" and a matched
    // cell's "BlankCard"); it follows the same hit test.
    update_hover_tip();
    timed_refresh();
}

// One click handler for the mouse and for --script-click, so a scripted run exercises the
// same navigation a mouse does.  Coordinates are scene coordinates.  Returns false when the
// window should close.
bool App::click_scene(float sx, float sy, bool motion_only) {
    // The difficulty chooser: three shipped buttons, hover art on motion, the choice starts the
    // game with the Professor's entrance and his "How To Play" bubbles.
    if (screen == Screen::Difficulty) {
        const UiButton* hit = nullptr;
        for (const UiButton& button : picker_buttons) {
            const SDL_FRect& r = button.rect;
            if (sx >= r.x && sx < r.x + r.w && sy >= r.y && sy < r.y + r.h) {
                hit = &button;
                break;
            }
        }
        for (UiButton& button : picker_buttons) {
            const bool hover = hit != nullptr && button.id == hit->id;
            if (button.hover == hover) continue;
            button.hover = hover;
            draw_difficulty();
        }
        if (motion_only || hit == nullptr) return true;
        std::printf("difficulty: %s chosen\n", hit->id.c_str());
        const std::string chosen = hit->id;
        if (picker_cakes) start_cakes(chosen == "Beginner" ? 0 : chosen == "Intermediate" ? 1 : 2);
        else start_game(chosen, true);
        return true;
    }
    // The end-of-round panel stands in for OnWinAnimStop's dialog box, so it is modal: only its
    // own buttons act and every other click is swallowed.
    if (result_shown) {
        const UiButton* hit = nullptr;
        for (const UiButton& button : result_buttons) {
            const SDL_FRect& r = button.rect;
            if (sx >= r.x && sx < r.x + r.w && sy >= r.y && sy < r.y + r.h) {
                hit = &button;
                break;
            }
        }
        // The chooser highlights on motion, so this panel does too; only the draws are rebuilt.
        bool hover_changed = false;
        for (UiButton& button : result_buttons) {
            const bool hover = hit != nullptr && button.id == hit->id;
            if (button.hover == hover) continue;
            button.hover = hover;
            hover_changed = true;
        }
        if (hover_changed) draw_result_panel();
        // The panel stands in for a modal dialog, so the board behind it holds no focus.
        if (motion_only) {
            if (screen == Screen::Concentration) update_hover(-1.0f, -1.0f);
            return true;
        }
        if (hit == nullptr) return true;
        const std::string action = hit->id; // rebuilding clears result_buttons
        if (screen == Screen::ComfyCakes) {
            if (action == "PlayAgain") {
                std::printf("cakes: New Game -> a new round\n");
                start_cakes(cakes_difficulty);
            } else if (action == "ChangeDifficulty") {
                std::printf("cakes: Change Difficulty -> the chooser\n");
                game_to_menu();
                picker_cakes = true;
                build_difficulty();
            } else {
                std::printf("cakes: Main Menu -> main menu\n");
                game_to_menu();
            }
            return true;
        }
        if (action == "PlayAgain") {
            std::printf("pairs: New Game -> a new %s round\n", picker_choice.c_str());
            ++seed_for_pairs;
            start_game(picker_choice, true);
        } else if (action == "ChangeDifficulty") {
            std::printf("pairs: Change Difficulty -> the chooser\n");
            hide_tip();
            free_direct(result_draws);
            result_buttons.clear();
            result_shown = false;
            rebuild_overlay();
            build_difficulty();
        } else {
            std::printf("pairs: Main Menu -> main menu\n");
            game_to_menu();
        }
        return true;
    }
    if (screen == Screen::Menu && menu_ready) {
        const Hotspot* hit = nullptr;
        for (const Hotspot& spot : menu_hotspots) {
            const SDL_FRect& r = spot.rect;
            if (sx >= r.x && sx < r.x + r.w && sy >= r.y && sy < r.y + r.h) {
                hit = &spot;
                break;
            }
        }
        const std::string want = hit == nullptr ? std::string{} : hit->name;
        if (want != menu_hover_name) {
            menu_hover_name = want;
            build_menu();  // slot 1 is the hover art; only rebuild when the target changes
        }
        if (motion_only || hit == nullptr) return true;
        if (hit->name == "ConcentrationButton") {
            // The shipped build reaches the game through its own "Select Difficulty" dialog;
            // this opens the in-canvas chooser instead.
            std::printf("menu: ConcentrationButton -> Purble Pairs difficulty chooser\n");
            picker_cakes = false;
            build_difficulty();
        } else if (hit->name == "ComfyCakesButton") {
            // PurblePlaceManager::Init sets its state to 2 for this house (0x100071A00 /
            // 0x100071A1A), which is what starts ComfyCakesGame::LoadGameUI.
            std::printf("menu: ComfyCakesButton -> Comfy Cakes difficulty chooser\n");
            picker_cakes = true;
            build_difficulty();
        } else if (hit->name == "ExitButton") {
            std::printf("menu: ExitButton clicked - exiting, as the game does\n");
            return false;
        } else {
            std::printf("menu: %s is not implemented in this reconstruction "
                        "(only Purble Pairs and Comfy Cakes are)\n",
                        hit->name.c_str());
        }
        return true;
    }
    // Comfy Cakes: the room's own buttons.  ReturnToMainMenu leaves; the two belt arrows and the
    // tool buttons pick an action, which is the next milestone, so for now a click is reported.
    if (screen == Screen::ComfyCakes) {
        const Hotspot* hit = nullptr;
        for (const Hotspot& spot : cakes_hotspots) {
            const SDL_FRect& r = spot.rect;
            if (sx >= r.x && sx < r.x + r.w && sy >= r.y && sy < r.y + r.h) {
                hit = &spot;
                break;
            }
        }
        if (motion_only) {
            const std::string want = hit == nullptr ? std::string{} : hit->name;
            if (want == cakes_hover_name) return true;
            cakes_hover_name = want;
            std::printf("  hover: %s\n", want.empty() ? "nothing" : want.c_str());
            // The whole refresh, not just build_cakes: the rebuild drops the draw list, so the
            // TV's target cake and the belt's cakes have to be appended again or the room shows
            // the background alone until the next click (cakes_refresh re-adds them, and the
            // menu's hover path is the same rebuild-and-redraw pattern).
            cakes_refresh();
            return true;
        }
        if (hit == nullptr) return true;
        if (hit->name == "ReturnToMainMenu") {
            std::printf("cakes: ReturnToMainMenu -> main menu\n");
            game_to_menu();
            return true;
        }
        if (hit->name == "SlideLeftButton") {
            cakes_slide_left();
            cakes_refresh();
            return true;
        }
        if (hit->name == "SlideRightButton") {
            cakes_slide_right();
            cakes_refresh();
            return true;
        }
        // A tool button.  Tool::UseTool takes the button's <Type> and applies it to the cake at
        // the tool's own station.
        const std::map<std::string, int>::const_iterator type = cakes_button_type.find(hit->name);
        if (type == cakes_button_type.end()) {
            std::printf("cakes: %s clicked (no layer type)\n", hit->name.c_str());
            return true;
        }
        cakes_use_tool(cakes_tool_station(hit->name), type->second);
        cakes_refresh();
        return true;
    }
    if (!pairs_ready) return true;
    // A tip bubble with <CaptureGlobalClick> (the default) takes the click, which is how the
    // intro's "How To Play" bubbles are dismissed in the shipped game.  It takes the click only:
    // the engine's mouse-move handler runs whatever is on screen, so the board behind the bubble
    // still moves its focus.
    if (tip.visible && tip.captures && !motion_only) {
        std::printf("  tip %s: closed\n", tip.id.c_str());
        professor_tip_closed();
        return true;
    }
    // Focus follows the mouse (see update_hover).  A click moves it too, so a scripted click
    // that never sent a motion event still highlights what it hit.
    update_hover(sx, sy);
    if (motion_only) return true;
    // Window -> scene -> card.  The target is the card's own node: the drawn sprite rect at
    // the cell's top-left, which at Beginner is 88x90 over a 74x74 cell, so the visible
    // card's right and bottom edges do work and neighbouring cells overlap by a few pixels.
    // The engine's node hit test takes the topmost node, so the scan runs index-descending.
    for (const Hotspot& spot : conc_hotspots) {
        const SDL_FRect& r = spot.rect;
        if (sx < r.x || sx >= r.x + r.w || sy < r.y || sy >= r.y + r.h) continue;
        if (spot.name == "ReturnToMainMenu") {
            std::printf("pairs: ReturnToMainMenu -> main menu\n");
            // The engine saves statistics and game state here (SaveGameStats 0x100082C70 /
            // SaveGameState 0x100082B40) and then sets state 4; this reconstruction does not
            // write save files.
            game_to_menu();
        } else if (spot.name == "NewGame") {
            std::printf("pairs: NewGame -> new round\n");
            start_game(picker_choice, true);
        } else if (spot.name == "ExitGame") {
            std::printf("pairs: ExitGame -> main menu\n");
            game_to_menu();
        }
        return true;
    }
    int hit = -1;
    for (std::size_t c = card_cache.size(); c-- > 0;) {
        const CardCache& entry = card_cache[c];
        if (entry.index < 0) continue;  // the highlight slot
        if (sx >= entry.base_x && sx < entry.base_x + entry.w &&
            sy >= entry.base_y && sy < entry.base_y + entry.h) {
            hit = entry.index;
            break;
        }
    }
    // Card::DoCardClick (0x10007CD65) tests Board+0x1E8 first: while the deal-in is still running
    // a click on a card stops the whole thing (Board::StopIntroAnimation 0x10008B45C, which brings
    // every card in at once) and does nothing else.  The screen's own buttons are not gated by
    // it, so ReturnToMainMenu and New Game still work while the cards are arriving.
    if (hit >= 0 && pairs_dealing) {
        std::printf("  deal: stopped by a click\n");
        finish_deal();
        timed_refresh();
        refresh_hud();
        return true;
    }
    // Card::HandleEvent (0x10007C1F4, case 1) only sends a click on to Card::DoCardClick from a
    // card the player can act on: state 0, whose back is up, or state 4 without the match flag.
    // State 3, the flip-forward, is refused, and the reconstruction's single flip covers the
    // whole turn in both directions, so a card that is still turning is not clickable here.
    if (hit >= 0 && hit < static_cast<int>(card_flip.size()) &&
        card_flip[static_cast<std::size_t>(hit)].phase != FlipPhase::None) {
        std::printf("  click %-3d -> ignored, the card is still turning\n", hit);
        return true;
    }
    if (hit < 0) {
        // The sneak-peek tokens: SneakPeakArea::HandleEvent (0x100084D3C) plays
        // Con_SneakPeek_Click (proto 0x7A) and starts the peek, which is Board::SneakPeaker
        // (0x10008B280) turning the whole board over for raw <SneakRevealDuration> ticks.
        if (pairs_board.tokens() <= 0) {
            for (const SDL_FRect& r : pairs_tokens) {
                if (sx < r.x || sx >= r.x + r.w || sy < r.y || sy >= r.y + r.h) continue;
                // SneakPeakArea::InitUI hides token i when `i + 1 > Round+0x11C`, so a
                // difficulty with <NumStartTokens>0 shows none and the click lands nowhere.
                std::printf("  sneak token: none left at this difficulty (NumStartTokens 0)\n");
                return true;
            }
            return true;
        }
        for (std::size_t t = 0; t < pairs_tokens.size(); ++t) {
            if (t >= static_cast<std::size_t>(pairs_board.tokens())) break;
            const SDL_FRect& r = pairs_tokens[t];
            if (sx < r.x || sx >= r.x + r.w || sy < r.y || sy >= r.y + r.h) continue;
            const int time_before = static_cast<int>(pairs_board.ticks_remaining());
            if (!pairs_board.use_token()) {
                std::printf("  sneak token %zu: no token left\n", t);
                return true;
            }
            play_pair_sfx("Con_SneakPeek_Click");
            play_pair_sfx("Con_SneakPeek_Starburst");
            pairs_sneak_frame = pairs_sneak_movie_ok ? 0.0f : -1.0f;
            std::printf("  sneak token %zu -> %d left, peek for %d ticks after flip, time cost %d ticks (%d -> %d)\n", t,
                        pairs_board.tokens(), pairs_constants.sneak_reveal_duration,
                        time_before - static_cast<int>(pairs_board.ticks_remaining()), time_before, static_cast<int>(pairs_board.ticks_remaining()));
            sync_flips();
            timed_refresh();
            refresh_hud();
            return true;
        }
        return true;
    }
    const pple::ClickResult clicked = pairs_board.click(hit);
    if (!clicked.accepted) return true;
    // Card::FlipBack (0x10007D060) plays nothing, unlike Card::FlipForward's Con_TileFlip, so a
    // card turned back down is silent and only the draw list and the highlight change.
    if (clicked.taken_back) {
        sync_flips();
        timed_refresh();
        refresh_hud();
        std::printf("  click %-3d -> turned back down\n", hit);
        return true;
    }
    play_click_sfx(clicked);
    // The mismatch pair's own bubble.  Card::DoMatcher (0x10007D224) arms the Professor for the
    // *matched* pair -- `ProFlipNoMatchTimed` while the level is timed and `ProFlipMatchUntimed`
    // when it is untimed, both with +0x48 = 150 and a MoveIn (0x10007D2CE / 0x10007D2E2) -- and
    // leaves him alone on a mismatch (`Con_TileFlip_Incorrect` at 0x10007D298 and a return), so
    // the bubble below is the reconstruction's own: the same tip, shown at the point the wrong
    // pair turns over, because the owner reads the silence as a missing cue.  A tip that is up
    // already -- the intro's, a win's -- keeps the screen.
    if (clicked.mismatch && (!tip.visible || tip.simple) && professor.anim_kind == 0)
        show_simple_tip(pairs_board.timed() ? "ProFlipNoMatchTimed" : "ProFlipMatchUntimed",
                        615.0f, 150.0f);
    // Card::DoForceMatch per matched card.  A completed board does not end the round here:
    // the jar's own state machine runs the flight and the burst and then calls
    // Jar::StopExplosion (finish_board_end).
    arm_match_fx(clicked.flew, false);
    if (clicked.relaid_out) {
        // The Mixer reshuffled the cards: Board::DoMixer re-places them, so the art has to be
        // rebuilt.  Deferred to the main loop so the board is never re-dealt from a click.
        pairs_rebuild_pending = true;
        pairs_keep_state = true;
        return true;
    }
    sync_flips();
    timed_refresh();
    refresh_hud();
    std::printf("  click %-3d -> %s, score %d, matches %d/%d%s\n", hit,
                clicked.match ? "MATCH"
                              : (clicked.wild_match
                                     ? "WILD (+10)"
                                     : (clicked.mismatch ? "MISS" : "revealed")),
                pairs_board.score(), pairs_board.matches(), pairs_board.round_pairs_total(),
                pairs_board.mismatch_ticks() >= 0 ? ", reveal timer armed" : "");
    return true;
}

} // namespace pple::viewer
