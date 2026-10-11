#include "pple/shop.hpp"

#include <algorithm>

namespace pple {
namespace {

// The engine's own strings (FeatureSet::Init 0x10006BC50 and FaceFeatureToName 0x1000F8B00).
const char* const kColors[5] = {"Red", "Purple", "Yellow", "Blue", "Green"};
const char* const kFeatures[5] = {"Hair", "Eye", "Nose", "Mouth", "Accessory"};
// The face template calls the fifth slot "Extras" (FaceFeatureToName's own table).
const char* const kFaceFeatures[5] = {"Hair", "Eye", "Nose", "Mouth", "Extras"};

// The engine's rand() stream is replaced by one deterministic generator so a seed reproduces a
// round.  The call order is the engine's: the template, then the 25 variants colour-major, then
// the hidden face's slots.  (A plain LCG was tried first and its low bits were far too regular to
// stand in for rand() % 5, so this is a splitmix64 step.)
class ShopRandom {
public:
    explicit ShopRandom(unsigned seed) : state_(0x9E3779B97F4A7C15ull * (seed + 1)) {}
    unsigned next() {
        state_ += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return static_cast<unsigned>((z ^ (z >> 31)) >> 33);
    }
    int below(int bound) { return bound <= 0 ? 0 : static_cast<int>(next() % static_cast<unsigned>(bound)); }

private:
    std::uint64_t state_ = 0;
};

}  // namespace

const char* shop_color_name(int color) {
    return color >= 0 && color < 5 ? kColors[color] : "";
}

const char* shop_feature_name(int feature) {
    return feature >= 0 && feature < 5 ? kFeatures[feature] : "";
}

std::string shop_face_item_name(int color, int feature, int variant) {
    if (color < 0 || color >= 5 || feature < 0 || feature >= 5) return {};
    return std::string(kColors[color]) + kFaceFeatures[feature] + std::to_string(variant + 1);
}

std::string shop_button_name(int color, int feature, int variant) {
    if (color < 0 || color >= 5 || feature < 0 || feature >= 5) return {};
    return std::string(kColors[color]) + kFeatures[feature] + std::to_string(variant + 1);
}

// Face::Compare (0x100069F18).  `hist` is the engine's own v22[14]: a multiset of the hidden
// face's colours, consumed one at a time as slots are matched, so a colour that is right but in
// the wrong place is counted only as often as the hidden face actually has it.
void ShopModel::compare(const std::array<int, 5>& hidden, const std::array<int, 5>& guess,
                        std::array<bool, 5>& matched, int& rcrf, int& rcwf) {
    int hist[14] = {};
    for (int slot = 0; slot < 5; ++slot)
        if (hidden[slot] != -1) ++hist[hidden[slot]];
    rcrf = 0;
    rcwf = 0;
    bool all_equal = true;
    for (int slot = 0; slot < 5; ++slot) {
        if (hidden[slot] == guess[slot]) {
            matched[slot] = true;
            if (hidden[slot] != -1) {
                ++rcrf;
                --hist[hidden[slot]];
            }
        } else {
            matched[slot] = false;
            all_equal = false;
        }
    }
    if (all_equal) return;
    for (int slot = 0; slot < 5; ++slot) {
        if (matched[slot]) continue;
        const int color = guess[slot];
        if (color == -1) continue;
        if (hist[color] > 0) {
            ++rcwf;
            --hist[color];
        }
    }
}

void ShopModel::new_game(int difficulty, unsigned seed) {
    difficulty_ = std::clamp(difficulty, 0, 2);
    ShopRandom rng(seed);
    const int count = kShopCounts[difficulty_];
    // BuildAPurbleGame::NewGame -> ApplyTemplateToAllFaces: six templates per difficulty.
    template_id_ = rng.below(6);
    // FeatureSet::Reset(..., bReset = 1): one variant per (colour, feature), colour-major.
    for (int color = 0; color < 5; ++color)
        for (int feature = 0; feature < 5; ++feature)
            variants_[static_cast<std::size_t>(5 * color + feature)] = rng.below(3);
    // Face::GenerateRandom(count, count): slots 0 and 4 are empty on Beginner, slot 4 on
    // Intermediate; the others take a random colour.
    hidden_.fill(-1);
    for (int slot = 0; slot < 5; ++slot) {
        if (!feature_in_play(slot)) continue;
        hidden_[static_cast<std::size_t>(slot)] = rng.below(count);
    }
    selection_.fill(-1);
    matched_.fill(false);
    history_.clear();
    last_ = ShopGuess{};
    guesses_ = 0;
    score_ = 100;  // Constants.xml InitialPoints
    won_ = false;
    lost_ = false;
}

int ShopModel::hidden(int feature) const {
    return feature >= 0 && feature < 5 ? hidden_[static_cast<std::size_t>(feature)] : -1;
}

int ShopModel::variant(int color, int feature) const {
    if (color < 0 || color >= 5 || feature < 0 || feature >= 5) return 0;
    return variants_[static_cast<std::size_t>(5 * color + feature)];
}

int ShopModel::selected(int feature) const {
    return feature >= 0 && feature < 5 ? selection_[static_cast<std::size_t>(feature)] : -1;
}

void ShopModel::set_selected(int feature, int color) {
    if (!feature_in_play(feature)) return;
    if (color < 0 || color >= count()) return;
    if (won_ || lost_) return;
    selection_[static_cast<std::size_t>(feature)] = color;
}

void ShopModel::select(int feature, int color) {
    // OnSetFeature's toggle: clicking the item that is already selected clears the slot.
    if (!feature_in_play(feature)) return;
    if (color < 0 || color >= count()) return;
    if (won_ || lost_) return;
    const std::size_t slot = static_cast<std::size_t>(feature);
    selection_[slot] = selection_[slot] == color ? -1 : color;
}

bool ShopModel::all_selected() const {
    for (int slot = feature_begin(); slot <= feature_end(); ++slot)
        if (selection_[static_cast<std::size_t>(slot)] == -1) return false;
    return true;
}

bool ShopModel::duplicate_guess() const {
    for (const ShopGuess& entry : history_)
        if (entry.colors == selection_) return true;
    return false;
}

int ShopModel::max_guesses() const {
    if (difficulty_ == 1) return 7;   // Constants.xml MaxGuessesINT
    if (difficulty_ == 2) return 13;  // Constants.xml MaxGuessesADV
    return 0;                         // Beginner is not limited (MakeGuess's v21 != 0 test)
}

ShopModel::Outcome ShopModel::make_guess() {
    Outcome outcome;
    if (won_ || lost_) return outcome;
    if (!all_selected()) return outcome;
    if (duplicate_guess()) {
        outcome.duplicate = true;
        return outcome;
    }
    outcome.accepted = true;
    ++guesses_;
    compare(hidden_, selection_, matched_, last_.rcrf, last_.rcwf);
    last_.colors = selection_;
    // The score only ever grows: RCRFPoints for every right colour in the right place and, on
    // Advanced, RCWFPoints for every right colour in the wrong place.
    score_ += last_.rcrf * 10;
    if (difficulty_ == 2) score_ += last_.rcwf * 5;
    bool all_equal = true;
    for (int slot = 0; slot < 5; ++slot)
        if (hidden_[static_cast<std::size_t>(slot)] != selection_[static_cast<std::size_t>(slot)])
            all_equal = false;
    if (all_equal) {
        won_ = true;
        outcome.won = true;
        return outcome;
    }
    const int limit = max_guesses();
    if (limit != 0 && guesses_ >= limit) {
        lost_ = true;
        outcome.lost = true;
        return outcome;
    }
    // HistoryWindow::AddEntry; the winning and losing guesses are not recorded.
    history_.push_back(last_);
    return outcome;
}

}  // namespace pple
