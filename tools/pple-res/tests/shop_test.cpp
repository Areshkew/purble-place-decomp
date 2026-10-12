// Purble Shop model test: the engine's comparison rule, the per-difficulty setup, the selection
// toggle, the score arithmetic and the end conditions.  No assets, no SDL.
#include "pple/shop.hpp"
#include "../src/viewer/shop_flow.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

using Slots = std::array<int, 5>;
}  // namespace

int main() {
    // Face::Compare: an exact match is three counts of the whole face.
    {
        const Slots hidden{0, 1, 2, -1, -1};
        std::array<bool, 5> matched{};
        int rcrf = 0, rcwf = 0;
        pple::ShopModel::compare(hidden, hidden, matched, rcrf, rcwf);
        require(rcrf == 3 && rcwf == 0, "exact match counts every occupied slot");
        require(matched[0] && matched[1] && matched[2], "exact match marks every occupied slot");
    }
    // Right colour, wrong feature: every colour is present but each in another slot.  The
    // multiset is consumed, so this is 0 right/wrong and 3 right-colour-wrong-feature.
    {
        const Slots hidden{0, 1, 2, -1, -1};
        const Slots guess{1, 2, 0, -1, -1};
        std::array<bool, 5> matched{};
        int rcrf = 0, rcwf = 0;
        pple::ShopModel::compare(hidden, guess, matched, rcrf, rcwf);
        require(rcrf == 0 && rcwf == 3, "a full rotation is three right-colour wrong-feature");
    }
    // A colour the hidden face does not have is not counted at all, and a colour it has twice is
    // counted only as often as it is present.
    {
        const Slots hidden{0, 0, 1, -1, -1};
        const Slots guess{1, 2, 2, -1, -1};
        std::array<bool, 5> matched{};
        int rcrf = 0, rcwf = 0;
        pple::ShopModel::compare(hidden, guess, matched, rcrf, rcwf);
        require(rcrf == 0 && rcwf == 1, "the duplicate colour is consumed once, the absent one never");
    }
    {
        const Slots hidden{0, 1, 2, -1, -1};
        const Slots guess{1, 0, 2, -1, -1};
        std::array<bool, 5> matched{};
        int rcrf = 0, rcwf = 0;
        pple::ShopModel::compare(hidden, guess, matched, rcrf, rcwf);
        require(rcrf == 1 && rcwf == 2, "one exact slot and two swapped colours");
    }
    {
        // The colour the hidden face has twice is consumed by the exact slot, so the slot that
        // moved is not counted; the second 1 has nothing left to match.
        const Slots hidden{0, 0, 1, -1, -1};
        const Slots guess{0, 1, 1, -1, -1};
        std::array<bool, 5> matched{};
        int rcrf = 0, rcwf = 0;
        pple::ShopModel::compare(hidden, guess, matched, rcrf, rcwf);
        require(rcrf == 2 && rcwf == 0, "the exact slot consumes the duplicate colour");
    }

    // Every difficulty: the features in play, the colour count and the variant range.
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        const int count = pple::kShopCounts[difficulty];
        for (unsigned seed = 1; seed <= 200; ++seed) {
            pple::ShopModel model;
            model.new_game(difficulty, seed);
            require(model.count() == count, "colour and feature count per difficulty");
            for (int slot = 0; slot < 5; ++slot) {
                if (model.feature_in_play(slot)) {
                    require(model.hidden(slot) >= 0 && model.hidden(slot) < count,
                            "a slot in play holds a colour of this difficulty");
                } else {
                    require(model.hidden(slot) == -1, "a slot out of play is empty");
                }
                for (int color = 0; color < 5; ++color) {
                    const int variant = model.variant(color, slot);
                    require(variant >= 0 && variant < 3, "three variants per colour and feature");
                }
            }
            require(!model.all_selected(), "a new round has nothing selected");
            require(model.score() == 100, "InitialPoints");
            require(!model.over(), "a new round is not over");
        }
    }

    // The selection toggle and the guess flow, played on a fixed seed so the hidden face is known.
    {
        pple::ShopModel model;
        model.new_game(0, 7);
        require(model.max_guesses() == 0, "Beginner has no guess limit");
        require(model.selected(1) == -1, "nothing selected yet");
        model.select(1, 2);
        require(model.selected(1) == 2, "selecting an item sets the slot's colour");
        model.select(1, 1);
        require(model.selected(1) == 1, "another colour replaces it");
        model.select(1, 1);
        require(model.selected(1) == -1, "clicking the selected item clears the slot");
        // A colour the difficulty does not have cannot be selected.
        model.select(1, 4);
        require(model.selected(1) == -1, "Beginner has no green");
        // A slot the difficulty does not use cannot be selected either.
        model.select(0, 0);
        require(model.selected(0) == -1, "Beginner does not use the hair shelf");
    }

    // A round that always guesses the hidden face wins on the first guess; the winning guess is
    // not appended to the history.
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        pple::ShopModel model;
        model.new_game(difficulty, 11);
        for (int slot = 0; slot < 5; ++slot)
            if (model.feature_in_play(slot)) model.select(slot, model.hidden(slot));
        require(model.all_selected(), "every slot in play is selected");
        const pple::ShopModel::Outcome outcome = model.make_guess();
        require(outcome.accepted && outcome.won, "the hidden face wins at once");
        require(model.rcrf() == model.count() && model.rcwf() == 0, "every occupied slot is right");
        require(model.score() == 100 + 10 * model.count(), "RCRFPoints per right feature");
        require(model.history().empty(), "the winning guess is not in the history");
        require(model.guesses() == 1, "the guess counter moved");
    }

    // Intermediate: a wrong guess is recorded, an identical one is refused, and the seventh
    // guess loses.  The guess uses one colour everywhere, which is only right for one slot at
    // most, so it can never be the winning guess.
    {
        pple::ShopModel model;
        model.new_game(1, 3);
        require(model.max_guesses() == 7, "MaxGuessesINT");
        bool lost = false;
        int accepted = 0;
        // Walk the legal selections in order, skipping the exact face and any repeat, until the
        // limit is reached.
        for (int k = 0; k < 4096 && !lost; ++k) {
            int value = k;
            bool exact = true;
            for (int slot = model.feature_begin(); slot <= model.feature_end(); ++slot) {
                const int color = value % model.count();
                value /= model.count();
                model.set_selected(slot, color);
                if (color != model.hidden(slot)) exact = false;
            }
            if (exact || model.duplicate_guess()) continue;
            const pple::ShopModel::Outcome outcome = model.make_guess();
            require(outcome.accepted, "a fresh full selection is judged");
            require(!outcome.won, "only the exact face wins");
            ++accepted;
            lost = outcome.lost;
        }
        require(accepted == 7, "seven judged guesses before the loss");
        require(lost, "the seventh wrong guess loses on Intermediate");
        require(model.guesses() == 7, "the loss lands on the limit");
    }

    // Advanced scores the right-colour wrong-feature count as well (RCWFPoints 5).  Use a round
    // whose hidden face has five different colours, so a rotation of it is 0 right / 5 misplaced.
    {
        pple::ShopModel model;
        bool distinct = false;
        // Five independent colours over five draws come out all-different in about 4% of rounds,
        // so a seed is searched for rather than assumed.
        for (unsigned seed = 1; seed <= 400 && !distinct; ++seed) {
            model.new_game(2, seed);
            distinct = true;
            for (int a = 0; a < 5 && distinct; ++a)
                for (int b = a + 1; b < 5 && distinct; ++b)
                    if (model.hidden(a) == model.hidden(b)) distinct = false;
        }
        require(distinct, "a seed with five different colours");
        require(model.max_guesses() == 13, "MaxGuessesADV");
        require(model.count() == 5, "Advanced uses five colours");
        for (int slot = 0; slot < 5; ++slot) model.set_selected(slot, (model.hidden(slot) + 1) % 5);
        require(!model.duplicate_guess(), "a fresh guess");
        const pple::ShopModel::Outcome outcome = model.make_guess();
        require(outcome.accepted && !outcome.won, "a rotation is not the exact face");
        require(model.rcwf() == 5 && model.rcrf() == 0, "every colour is right, in the wrong place");
        require(model.score() == 100 + 5 * 5, "RCWFPoints on Advanced");
    }

    // Fast replacements never add extra face parts: each shelf has exactly one slot.
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        pple::ShopModel model;
        model.new_game(difficulty, 1);
        for (int feature = model.feature_begin(); feature <= model.feature_end(); ++feature) {
            for (int color = 0; color < model.count(); ++color) {
                model.select(feature, color);
                require(model.selected(feature) == color, "replacement owns one feature slot");
            }
            model.select(feature, model.count() - 1);
            require(model.selected(feature) == -1, "click selected part clears only that shelf");
        }
    }
    // Curtain timeline and count-feedback lifetime are independent of model guesses.
    {
        using Flow = pple::viewer::ShopFlow;
        Flow flow;
        require(flow.curtain_frame() == 0 && flow.mystery_visible(), "intro starts closed");
        flow.advance(0.5f);
        require(flow.curtain_frame() == 7 && flow.mystery_visible(), "intro opens on mystery");
        flow.advance(3.25f);
        require(flow.curtain_frame() == 11, "intro closes after hold");
        flow.advance(0.25f);
        require(flow.phase == Flow::Phase::Playing && flow.curtain_frame() == 0 &&
                !flow.mystery_visible() && !flow.answer_visible(), "guessing hides mystery and answer");
        flow.counts_visible = true;
        flow.reveal();
        require(flow.answer_visible() && flow.curtain_frame() == 0, "answer behind closed curtain");
        flow.advance(0.25f);
        require(flow.curtain_frame() == 3 && flow.moving(), "answer opening in progress");
        flow.advance(0.25f);
        require(flow.phase == Flow::Phase::Answer && flow.curtain_frame() == 7 &&
                !flow.moving(), "answer remains visible until player continues");
        flow.advance(20.0f);
        require(flow.answer_visible(), "answer does not disappear on a timer");
        flow.reset();
        require(!flow.counts_visible && !flow.answer_visible() && flow.curtain_frame() == 0,
                "restart cancels old reveal and feedback");
    }

    // Names: the face template calls the fifth slot Extras, the cabinet calls it Accessory.
    require(pple::shop_face_item_name(0, 1, 1) == "RedEye2", "face item name");
    require(pple::shop_button_name(0, 1, 1) == "RedEye2", "cabinet item name");
    require(pple::shop_face_item_name(1, 4, 0) == "PurpleExtras1", "the face's fifth slot is Extras");
    require(pple::shop_button_name(1, 4, 0) == "PurpleAccessory1",
            "the cabinet's fifth slot is Accessory");
    require(std::string(pple::shop_color_name(3)) == "Blue", "colour name");
    require(std::string(pple::shop_feature_name(3)) == "Mouth", "feature name");

    std::printf("shop model: comparison, difficulty setup, selection, scoring and end conditions OK\n");
    return 0;
}
