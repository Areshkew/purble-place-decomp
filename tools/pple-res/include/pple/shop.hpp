// The Purble Shop (Build-a-Purble) rules, with no SDL.  Everything here comes from the binary:
//
//   the game document            UI\BuildAPurble\BuildAPurbleGame.xml (BuildAPurbleGame::LoadGameUI
//                                0x100050B54): Background, ColorCountWidget, HiddenPurble,
//                                CurtainRod, Curtain, Spotlight/Spotlight2, Cabinet, Stage,
//                                GuessButton, GuessHistory, GuessPurble, the labels and buttons
//   the constants                UI\BuildAPurble\Constants.xml, read by BuildAPurbleGame::InitGame
//                                (0x10005194C): MaxGuessesINT 7, MaxGuessesADV 13, RCRFPoints 10,
//                                RCWFPoints 5, InitialPoints 100, NegHintGuessLimit 4.
//                                RemoveFirstFeature/RemoveNextFeature exist in the file but no
//                                code reads them in this build.
//   the colours                  FeatureSet::Init (0x10006BC50) string table: Red, Purple, Yellow,
//                                Blue, Green -- indices 0..4
//   the features                 the same function's table: Hair, Eye, Nose, Mouth, Accessory
//                                (the *face* feature name for index 4 is "Extras"; the cabinet's
//                                buttons are named "Accessory", FaceFeatureToName 0x1000F8B00)
//   the per-difficulty ranges    FeatureSet::SHELF_FEATURE_RANGES (0x1000F8AE0) = {1,3},{0,3},{0,4}
//   the variant count            Face::FaceFeatureMultiples (0x1000F9140): 3 for every colour and
//                                feature, so one cabinet item per colour is visible at a time and
//                                the visible one is the variant this round picked
//   the guess comparison         Face::Compare (0x100069F18)
//   the guess flow               BuildAPurbleGame::MakeGuess (0x1000527CC)
//   the round setup              BuildAPurbleGame::NewGame (0x100052528) and
//                                ApplyTemplateToAllFaces (0x100054BB8)
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pple {

// The five colours, in the engine's own order (FeatureSet::Init's string table).
enum class ShopColor : int { Red = 0, Purple = 1, Yellow = 2, Blue = 3, Green = 4 };
// The five feature slots, in the engine's own order.
enum class ShopFeature : int { Hair = 0, Eye = 1, Nose = 2, Mouth = 3, Extras = 4 };

// The number of colours and features a difficulty uses, and the feature slots it fills.  Both
// come from qword_100014650 and SHELF_FEATURE_RANGES: Beginner 3 colours / Eye..Mouth,
// Intermediate 4 colours / Hair..Mouth, Advanced 5 colours / Hair..Extras.
constexpr int kShopCounts[3] = {3, 4, 5};
constexpr int kShopFeatureBegin[3] = {1, 0, 0};
constexpr int kShopFeatureEnd[3] = {3, 3, 4};

const char* shop_color_name(int color);
const char* shop_feature_name(int feature);
// The name of the face-template sprite for one item: "<Color><Feature><variant+1>", e.g. "RedEye2".
// Face::GetFeatureName (0x100069BFC) appends the variant only when FaceFeatureMultiples says the
// colour has more than one, which is true for all of them here.
std::string shop_face_item_name(int color, int feature, int variant);
// The name of the cabinet's own button for the same item: index 4 is "Accessory", not "Extras".
std::string shop_button_name(int color, int feature, int variant);

// One judged guess: the two counts the colour-count widget shows and the colours that were guessed
// (the history window stores exactly this, HistoryWindow::AddEntry 0x10006EB98).
struct ShopGuess {
    int rcrf = 0;   // right colour, right feature
    int rcwf = 0;   // right colour, wrong feature
    std::array<int, 5> colors{-1, -1, -1, -1, -1};
};

// A whole round.  The model owns the hidden face, the cabinet's variant selection, the player's
// selection, the counters and the score; the viewer only moves it and draws it.
class ShopModel {
public:
    // BuildAPurbleGame::NewGame: pick one of the six templates for the difficulty, randomise every
    // (colour, feature) variant, reset the selection counters, generate the hidden face and close
    // the curtain.  `seed` stands in for the engine's rand() stream.
    void new_game(int difficulty, unsigned seed);

    int difficulty() const { return difficulty_; }
    int count() const { return kShopCounts[difficulty_]; }
    int feature_begin() const { return kShopFeatureBegin[difficulty_]; }
    int feature_end() const { return kShopFeatureEnd[difficulty_]; }
    bool feature_in_play(int feature) const {
        return feature >= feature_begin() && feature <= feature_end();
    }
    // -1 for a feature this difficulty does not use.
    int hidden(int feature) const;
    // Which of the three variants of `color`/`feature` this round shows; 0..2.
    int variant(int color, int feature) const;
    // The player's colour per feature slot, -1 when nothing is selected yet.
    int selected(int feature) const;
    // A click on the visible item for (feature, color): selects it, or clears the slot when that
    // colour was already selected (BuildAPurbleGame::OnSetFeature 0x100053C5C's toggle).
    void select(int feature, int color);
    // The same slot without the toggle, for a scripted round and for the viewer's solve hook.
    void set_selected(int feature, int color);
    bool all_selected() const;
    // True when an identical guess has already been made; MakeGuess refuses those (0x1000528C5).
    bool duplicate_guess() const;

    // The guess button.  Fills `last`, updates the counters and the score, appends the history
    // entry and reports whether the game was won or lost.
    struct Outcome {
        bool accepted = false;
        bool duplicate = false;
        bool won = false;
        bool lost = false;
    };
    Outcome make_guess();

    const ShopGuess& last() const { return last_; }
    const std::vector<ShopGuess>& history() const { return history_; }
    // The engine's paFeatures array: the slots whose colour is right AND in the right place.  The
    // Beginner mode's shelf marks are drawn from this (0x100052B02).
    const std::array<bool, 5>& matched() const { return matched_; }

    int guesses() const { return guesses_; }
    int max_guesses() const;   // 7 Intermediate, 13 Advanced; Beginner has no limit
    // The counts the colour-count widget shows: the engine keeps them at stats+28/+32, which
    // Face::Compare writes and MakeGuess reads, so they are the last judged guess's counts.
    int rcrf() const { return last_.rcrf; }
    int rcwf() const { return last_.rcwf; }
    int score() const { return score_; }
    bool won() const { return won_; }
    bool lost() const { return lost_; }
    bool over() const { return won_ || lost_; }
    int template_id() const { return template_id_; }

    // Face::Compare (0x100069F18): the multiset comparison the two counts come from.  Exposed
    // separately so a test can check it against the engine's rule without a round.
    static void compare(const std::array<int, 5>& hidden, const std::array<int, 5>& guess,
                        std::array<bool, 5>& matched, int& rcrf, int& rcwf);

private:
    int difficulty_ = 0;
    int template_id_ = 0;
    std::array<int, 25> variants_{};   // [5 * color + feature], 0..2
    std::array<int, 5> hidden_{-1, -1, -1, -1, -1};
    std::array<int, 5> selection_{-1, -1, -1, -1, -1};
    std::array<bool, 5> matched_{};
    std::vector<ShopGuess> history_;
    ShopGuess last_{};
    int guesses_ = 0;
    int score_ = 100;
    bool won_ = false;
    bool lost_ = false;
};

}  // namespace pple
