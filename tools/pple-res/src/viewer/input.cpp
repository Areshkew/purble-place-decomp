#include "app.hpp"
#include "audio.hpp"

namespace pple::viewer {

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
        start_game(hit->id, true);
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
            build_difficulty();
        } else if (hit->name == "ExitButton") {
            std::printf("menu: ExitButton clicked - exiting, as the game does\n");
            return false;
        } else {
            std::printf("menu: %s is not implemented in this reconstruction "
                        "(only Purble Pairs is)\n", hit->name.c_str());
        }
        return true;
    }
    if (!pairs_ready) return true;
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
        if (motion_only || hit == nullptr) return true;
        if (hit->id == "PlayAgain") {
            std::printf("pairs: New Game -> a new %s round\n", picker_choice.c_str());
            ++seed_for_pairs;
            start_game(picker_choice, true);
        } else if (hit->id == "ChangeDifficulty") {
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
    // A tip bubble with <CaptureGlobalClick> (the default) takes the click, which is how the
    // intro's "How To Play" bubbles are dismissed in the shipped game.
    if (tip.visible && tip.captures) {
        std::printf("  tip %s: closed\n", tip.id.c_str());
        professor_tip_closed();
        return true;
    }
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
    play_click_sfx(clicked);
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
                pairs_board.lockout_ticks() > 0 ? ", reveal timer armed" : "");
    return true;
}

} // namespace pple::viewer
