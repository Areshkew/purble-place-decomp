#include "app.hpp"
#include "audio.hpp"

namespace pple::viewer {

// Jar::StopExplosion (0x100083A20): the board has finished bursting, so either the round is
// won or Board::NewLevel deals the next <Level>.  The scripted hooks call this directly,
// because they have no frames in which to animate the flight and the burst.
void App::finish_board_end() {
    if (!pairs_board.end_pending()) return;
    const pple::ClickResult r = pairs_board.end_board();
    pairs_arcs.clear();
    pairs_jar_phase = JarPhase::Idle;
    pairs_burst_frame = 0.0f;
    if (r.level_up) {
        // Jar::StopExplosion (0x100083A20) picks the between-boards tip from the difficulty
        // (Round+0x0C: 1 is Intermediate, 2 is Advanced), sets Professor+0x48 = 125 and calls
        // MoveIn, then Board::NewLevel re-deals the next <Level> behind the bubble.
        pending_level_tip = pairs_board.level().difficulty_index == 1 ? "ProIntLevelChange"
                                                                     : "ProAdvLevelChange";
        std::printf("    board relaid out -> level %d/%d\n", pairs_board.level_position() + 1,
                    pairs_board.level_count());
        pairs_rebuild_pending = true;
        pairs_keep_state = true;
    } else if (r.board_cleared) {
        std::printf("  board cleared -> round WON at level %d/%d\n",
                    pairs_board.level_position() + 1, pairs_board.level_count());
    }
}

// End of round.  Board::Update's loss goes straight to OnEnterState(3), and
// OnEnterWonGameState is reached from Jar::StopExplosion, so `round_over` is the signal and
// `won()` alone is not.  Con_Win / Con_GameOver are the sounds OnEnterWonGameState /
// OnEnterLoseGameState play, and CalcEndGame's total and rank are what the game's own end
// dialog shows -- not the status bar's Round+0x120.
void App::report_round_end() {
    if (!pairs_board.round_over() || pairs_end_played) return;
    pairs_end_played = true;
    if (pairs_board.round_won()) {
        play_pair_sfx("Con_Win");
        std::printf("  round over: WON at level %d/%d, score %d\n",
                    pairs_board.level_position() + 1, pairs_board.level_count(),
                    pairs_board.score());
        pple::ConcentrationBoard::EndGame end;
        if (pairs_ranks_ok) {
            end = pairs_board.calc_end_game(pairs_ranks);
            last_end = end;
            last_end_ok = true;
            std::printf("  end game: points %d + time bonus %d (%d%%) + flip bonus %d "
                        "(clicks %d, ratio %.3f) = %d -> rank %d, \"%s\"\n",
                        end.points, end.time_bonus, end.time_percent, end.flip_bonus,
                        pairs_board.clicks(), end.flip_ratio, end.total, end.rank + 1,
                        end.title.c_str());
        }
        // ConcentrationGame::SaveGameStats (0x100082C70) writes the statistics through
        // XmlStash::SaveXMLFile (0x100079A00) into the RGMH container.  It is opt-in here so
        // a viewer run never touches the player's own file.
        if (!pairs_save_path.empty()) {
            pple::ConSettings settings;
            const std::vector<std::uint8_t> existing = slurp_file(pairs_save_path);
            pple::RgmhFile container;
            std::string save_error;
            if (!existing.empty() && pple::rgmh_read(existing, container, save_error) &&
                pple::con_settings_parse(container.xml, settings, save_error)) {
                std::printf("  save: loaded %s (%zu settings bytes)\n", pairs_save_path.c_str(),
                            container.xml.size());
            } else {
                // A fresh file: the reader only checks magic, version and header_size, so a
                // zero GUID and no thumbnail are accepted.  The game writes a thumbnail of
                // the finished board; this does not.
                container = pple::RgmhFile{};
                container.project_name = u"Purble Place";
                container.file_name = u"ConcentrationScoresAndSettings.xml";
                settings.disabled_tips = {"SneakPeak", "SneakFlip", "ProIntroNext", "ProIntro",
                                          "JokerFlip",  "Jar1",      "EmptySneak",   "BlankCard"};
                std::printf("  save: %s does not exist, starting from a default\n",
                            pairs_save_path.c_str());
            }
            const int difficulty = pairs_board.level().difficulty_index;
            // OnEnterWonGameState: Round+0x158 += Round+0x12C (the CURRENT level's matched
            // cards) and Round+0x128 (Clicks) is what TilesTurned accumulates.
            pple::con_settings_record_win(settings, difficulty, end.total,
                                          static_cast<std::uint64_t>(std::time(nullptr)),
                                          static_cast<std::uint32_t>(pairs_board.cards_removed()),
                                          static_cast<std::uint32_t>(pairs_board.clicks()));
            container.xml = pple::con_settings_payload(settings);
            container.xml_length = static_cast<std::uint32_t>(container.xml.size());
            const std::vector<std::uint8_t> out = pple::rgmh_write(container);
            if (write_file(pairs_save_path, out)) {
                std::printf("  save: wrote %s, %zu bytes, %zu settings bytes\n",
                            pairs_save_path.c_str(), out.size(), container.xml.size());
            } else {
                std::printf("  save: could not write %s\n", pairs_save_path.c_str());
            }
        }
        // OnEnterWonGameState (0x1000824E0): the Professor's win movie and a random ProWin tip,
        // then MoveIn.  OnWinAnimStop (0x100082670) opens the score dialog when the sequence is
        // over, which is where build_result_panel takes over.
        professor_round_end(true);
    } else {
        play_pair_sfx("Con_GameOver");
        std::printf("  round over: LOST, score %d\n", pairs_board.score());
        // OnEnterLoseGameState (0x1000825FF): Con_GameOver, a random ProLose tip, MoveIn.
        professor_round_end(false);
    }
}

} // namespace pple::viewer
