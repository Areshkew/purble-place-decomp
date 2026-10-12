#include "pple/concentration.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

struct Fixture {
    pple::CardPools pools;
    pple::ConcentrationConstants constants;
    pple::ConcentrationBoard board;
    Fixture(int tokens, bool eye_first = true, bool wild = false) {
        pools.cards = {{0, 0}, {1, 0}, {2, 0}, {9, 8}};
        pools.normal = {0, 1, 2};
        pools.wild = 9;
        constants.match_points_normal = 10;
        constants.sneak_match_bonus = 20;
        constants.sneak_reveal_duration = 45;
        constants.reveal_duration = 8;
        constants.sneak_time_decrement = 5;
        pple::ConcentrationLevel level;
        level.num_cols = 3;
        level.num_rows = 2;
        level.card_pairs = "AAA";
        level.num_card_types = 3;
        level.num_unique_pairs = 3;
        level.total_time_seconds = 210;
        level.num_start_tokens = tokens;
        check(board.reset({level}, 0, pools, constants, {}, 0), "reset fixture");
        pple::ConcentrationBoard::RestoreState state;
        state.card_ids = {wild ? 9 : 0, 0, 1, 1, 2, 2};
        state.faces.assign(6, pple::CardFace::Hidden);
        state.sneak_icon = {eye_first, !eye_first, false, false, false, false};
        state.tokens = tokens;
        state.ticks_left = level.total_ticks();
        check(board.restore(state), "restore deterministic eye fixture");
    }
};
}

int main() {
    for (int tokens = 0; tokens <= 3; ++tokens) {
        for (bool eye_first : {false, true}) {
            Fixture f(tokens, eye_first);
            check(f.board.click(0).accepted, "first card accepted");
            check(f.board.tokens() == tokens, "first reveal does not award token");
            const auto result = f.board.click(1);
            check(result.match && result.sneak_get, "matching either eye position awards bonus");
            check(f.board.tokens() == (tokens < 3 ? tokens + 1 : 3), "eye adds token capped at three");
            check(f.board.score() == 50, "eye score bonus unchanged");
            check(!f.board.click(1).accepted, "matched eye cannot award twice");
            const int awarded = f.board.tokens();
            const int before = static_cast<int>(f.board.ticks_remaining());
            check(f.board.use_token(), "earned token is usable");
            check(f.board.tokens() == awarded - 1, "using token removes one");
            check(before - static_cast<int>(f.board.ticks_remaining()) == 315, "peek time cost unchanged");
            check(f.board.face(2) == pple::CardFace::Shown, "earned peek reveals remaining cards");
            check(!f.board.use_token(), "cannot use token during peek");
            f.board.tick(54);
            check(f.board.sneak_active(), "peek keeps flip delay and 45-tick duration");
            f.board.tick(1);
            check(!f.board.sneak_active() && f.board.face(2) == pple::CardFace::Hidden,
                  "earned peek expires and hides remaining cards");
            check(f.board.face(0) == pple::CardFace::Matched, "peek preserves matched cards");
        }
    }
    {
        Fixture f(0);
        f.board.click(0);
        check(f.board.click(2).mismatch, "eye mismatch");
        check(f.board.tokens() == 0 && f.board.score() == 0, "mismatch awards nothing");
        f.board.tick(19);
        f.board.click(2);
        check(!f.board.click(3).sneak_get && f.board.tokens() == 0, "ordinary match awards no token");
        f.board.click(0);
        check(f.board.click(1).sneak_get && f.board.tokens() == 1, "eye can award after mismatch");
    }
    {
        Fixture f(0, false, true);
        f.board.click(0);
        const auto result = f.board.click(1);
        check(result.wild_match && result.sneak_get && f.board.tokens() == 1,
              "wild match with eye awards usable token");
    }
    std::cout << "PASS: eye reward, three-token cap, mismatch, wild match and earned peek lifecycle\n";
}
