#include "pple/concentration.hpp"

#include <algorithm>
#include <cstdlib>
#include <random>

#include "pple/xml.hpp"

namespace pple {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const XmlNode* child_ci(const XmlNode& node, const std::string& name) {
    const std::string want = lower(name);
    for (const XmlNode& c : node.children)
        if (lower(c.name) == want) return &c;
    return nullptr;
}

bool child_int_ci(const XmlNode& node, const std::string& name, int& out) {
    const XmlNode* c = child_ci(node, name);
    if (c == nullptr) return false;
    out = std::atoi(c->text.c_str());
    return true;
}

int child_int(const XmlNode& node, const std::string& name, int fallback) {
    int value = fallback;
    child_int_ci(node, name, value);
    return value;
}

}  // namespace

const CardDef* CardPools::find(int id) const {
    for (const CardDef& c : cards)
        if (c.id == id) return &c;
    return nullptr;
}

bool concentration_pools_parse(std::span<const std::uint8_t> cards_xml, CardPools& pools,
                              std::string& error) {
    pools = CardPools{};
    XmlNode root;
    if (!xml_parse(cards_xml, root, error)) return false;
    for (const XmlNode& card : root.children) {
        if (lower(card.name) != "card") continue;
        CardDef def;
        def.id = child_int(card, "id", -1);
        def.type = child_int(card, "type", -1);
        def.chef = child_int(card, "chefitem", 0) != 0;
        def.path = card.child_text("path");
        def.desc = card.child_text("desc");
        if (def.id < 0) continue;
        pools.cards.push_back(def);
        // Board::BuildCardLists: switch on <type>; everything else (including 0) goes to the
        // "normal" list.
        switch (def.type) {
            case 1: pools.chef = def.id; break;
            case 3: pools.mixer = def.id; break;
            case 4: pools.tricky.push_back(def.id); break;
            case 5: pools.super_tricky.push_back(def.id); break;
            case 6: pools.time_bonus = def.id; break;
            case 7: pools.unassigned = def.id; break;
            case 8: pools.wild = def.id; break;
            default: pools.normal.push_back(def.id); break;
        }
    }
    if (pools.cards.empty()) {
        error = "no <Card> entries";
        return false;
    }
    return true;
}

bool concentration_game_parse(std::span<const std::uint8_t> scene_xml, ConcentrationConstants& constants,
                             std::vector<ConcentrationLevel>& levels, std::string& error) {
    constants = ConcentrationConstants{};
    levels.clear();
    XmlNode root;
    if (!xml_parse(scene_xml, root, error)) return false;

    if (const XmlNode* c = child_ci(root, "Constants")) {
        constants.happy_chef_duration = child_int(*c, "HappyChefDuration", 0);
        constants.sneak_match_bonus = child_int(*c, "SneakMatchBonus", 0);
        constants.sneak_peak_icon_freq_seconds = child_int(*c, "SneakPeakIconFreq", 0);
        constants.sneak_peak_icon_duration_seconds = child_int(*c, "SneakPeakIconDur", 0);
        constants.board_border_width = child_int(*c, "BoardBorderWidth", 0);
        constants.board_border_height = child_int(*c, "BoardBorderHeight", 0);
        constants.match_points_normal = child_int(*c, "MatchPointsNormal", 0);
        constants.match_points_happy_chef = child_int(*c, "MatchPointsHappyChef", 0);
        constants.match_points_sad_chef = child_int(*c, "MatchPointsSadChef", 0);
        constants.match_points_blender = child_int(*c, "MatchPointsBlender", 0);
        constants.match_points_tricky = child_int(*c, "MatchPointsTricky", 0);
        constants.match_points_super_tricky = child_int(*c, "MatchPointsSuperTricky", 0);
        constants.reveal_duration = child_int(*c, "RevealDuration", 0);
        constants.sneak_reveal_duration = child_int(*c, "SneakRevealDuration", 0);
        constants.time_bonus_percent = child_int(*c, "TimeBonusPercent", 0);
        constants.sneak_time_decrement = child_int(*c, "SneakTimeDecrement", 0);
    }

    const XmlNode* modes = child_ci(root, "DifficultyModes");
    if (modes == nullptr) {
        error = "no <DifficultyModes>";
        return false;
    }
    static const char* kNames[3] = {"Beginner", "Intermediate", "Advanced"};
    for (int d = 0; d < 3; ++d) {
        const XmlNode* node = child_ci(*modes, kNames[d]);
        if (node == nullptr) continue;
        const int start_tokens = child_int(*node, "NumStartTokens", -1);
        const int cols = child_int(*node, "NumCols", -1);
        const int rows = child_int(*node, "NumRows", -1);
        const int xs = child_int(*node, "XSpacing", -1);
        const int ys = child_int(*node, "YSpacing", -1);
        const int tricky = child_int(*node, "NumTricky", -1);
        const int trickier = child_int(*node, "NumTrickier", -1);
        const XmlNode* levels_node = child_ci(*node, "Levels");
        if (levels_node == nullptr) continue;
        int level_index = 0;
        for (const XmlNode& lvl : levels_node->children) {
            if (lower(lvl.name) != "level") continue;
            ConcentrationLevel out;
            out.difficulty = kNames[d];
            out.difficulty_index = d;
            out.level_index = level_index++;
            out.num_start_tokens = start_tokens;
            out.num_cols = cols;
            out.num_rows = rows;
            out.x_spacing = xs;
            out.y_spacing = ys;
            out.num_tricky = tricky;
            out.num_trickier = trickier;
            out.total_time_seconds = child_int(lvl, "TotalTime", -1);
            out.num_card_types = child_int(lvl, "NumCardTypes", -1);
            out.num_unique_pairs = child_int(lvl, "NumUniquePairs", -1);
            out.card_pairs = lvl.child_text("CardPairs");
            levels.push_back(std::move(out));
        }
    }
    if (levels.empty()) {
        error = "no <Levels>/<Level> entries";
        return false;
    }
    return true;
}

bool concentration_cards_box_parse(std::span<const std::uint8_t> scene_xml, CardsBox& box,
                                   std::string& error) {
    box = CardsBox{};
    XmlNode root;
    if (!xml_parse(scene_xml, root, error)) return false;
    const XmlNode* cards = child_ci(root, "Cards");
    if (cards == nullptr) {
        error = "no <Cards>";
        return false;
    }
    box.width = child_int(*cards, "Width", box.width);
    box.height = child_int(*cards, "Height", box.height);
    return true;
}

std::vector<int> concentration_build_deck(const ConcentrationLevel& level, const CardPools& pools,
                                         unsigned seed) {
    // Board::InitLevel: the cell count is NumCols * NumRows and an odd count loses its last
    // cell here (it is re-added as a lone Wild Card by ShuffleCards).
    int cells = level.cell_count();
    if (cells & 1) --cells;
    std::mt19937 rng(seed);
    std::vector<int> normal = pools.normal;
    std::vector<int> tricky = pools.tricky;
    std::vector<int> super_tricky = pools.super_tricky;

    // NumTrickier / NumTricky decide how many plain letters come from the scarcer pools.  The
    // engine's counters default to -1 when the element is absent, and a counter that is <= 0
    // falls through to the next pool.
    int trickier_left = level.num_trickier;
    int tricky_left = level.num_tricky;

    const auto draw_from = [&](std::vector<int>& pool, int& counter) -> int {
        if (pool.empty()) return -1;
        std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
        const std::size_t at = pick(rng);
        const int id = pool[at];
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(at));
        if (counter > 0) --counter;
        return id;
    };

    std::vector<int> deck;
    deck.reserve(static_cast<std::size_t>(cells));
    std::size_t letter = 0;
    int previous_type = -1;
    char previous_char = 0;
    while (deck.size() + 2 <= static_cast<std::size_t>(cells)) {
        const char ch = letter < level.card_pairs.size() ? level.card_pairs[letter] : previous_char;
        ++letter;
        int id = -1;
        if (ch == previous_char && previous_type >= 0) {
            // Board::GetCardList's "current letter equals the previous one" path copies the
            // card it just added, so a run of the same letter repeats the previous pair's type.
            // That is what makes the number of distinct faces equal the number of runs, and
            // equal to <NumCardTypes> for Beginner.
            id = previous_type;
        } else {
            switch (ch) {
                case 'P': id = pools.chef; break;
                case 'M': id = pools.mixer; break;
                case 'T': id = pools.time_bonus; break;
                case 'J': id = pools.wild; break;
                case 'Z': {
                    const bool first = (rng() % 10) >= 5;
                    id = first ? pools.chef : pools.wild;
                    break;
                }
                default:
                    if (trickier_left > 0) {
                        id = draw_from(super_tricky.empty() ? tricky : super_tricky, trickier_left);
                    } else if (tricky_left > 0) {
                        id = draw_from(tricky, tricky_left);
                    } else {
                        int unused = 0;
                        id = draw_from(normal, unused);
                    }
                    break;
            }
        }
        if (id < 0) {
            // Pool exhausted; fall back to the normal pool and then to any card at all.
            int unused = 0;
            id = draw_from(normal, unused);
        }
        if (id < 0 && !pools.cards.empty()) id = pools.cards.front().id;
        previous_type = id;
        previous_char = ch;
        deck.push_back(id);
        deck.push_back(id);
    }
    // ShuffleCards: the ids are drawn out of the list at random, so the board order is random.
    std::shuffle(deck.begin(), deck.end(), rng);
    return deck;
}

std::vector<ConcentrationSlot> concentration_layout(const ConcentrationLevel& level, int cards_width) {
    std::vector<ConcentrationSlot> slots;
    if (level.num_cols <= 0 || level.num_rows <= 0) return slots;
    // Board::ShuffleCards, verbatim.  Note the swapped-looking divisor: v17 divides by
    // NumRows while using XSpacing, and v18 divides by NumCols while using YSpacing.
    const int cell_w = (430 - level.x_spacing * (level.num_rows + 1)) / level.num_rows;
    const int cell_h = (434 - level.y_spacing * (level.num_cols + 1)) / level.num_cols;
    const int scale = cards_width > 0 ? static_cast<int>(static_cast<double>(cell_w) /
                                                         static_cast<double>(cards_width) * 100.0)
                                      : 100;

    int index = 0;
    for (int row = 0; row < level.num_rows; ++row) {
        for (int col = 0; col < level.num_cols; ++col) {
            ConcentrationSlot slot;
            slot.index = index++;
            slot.col = col;
            slot.row = row;
            slot.x = col * (level.x_spacing + cell_w) + level.x_spacing + 13;
            slot.y = row * (level.y_spacing + cell_h) + level.y_spacing + 13;
            slot.w = cell_w;
            slot.h = cell_h;
            slot.scale_percent = scale;
            slots.push_back(slot);
        }
    }
    return slots;
}

// ---------------------------------------------------------------------------------------

int ConcentrationBoard::match_score(int card_type, int multiplier_input) const {
    // Board::MatchMade.
    int points = constants_.match_points_normal;
    if (card_type == 4) points = constants_.match_points_tricky;
    else if (card_type == 5) points = constants_.match_points_super_tricky;
    if (multiplier_input > 1) {
        if (multiplier_input == 2) points *= 2;
    } else {
        points *= 3;
    }
    return points;
}

bool ConcentrationBoard::reset(const ConcentrationLevel& level, const CardPools& pools,
                              const ConcentrationConstants& constants, const CardsBox& box,
                              unsigned seed) {
    level_ = level;
    constants_ = constants;
    pools_ = &pools;
    deck_ = concentration_build_deck(level, pools, seed);
    slots_ = concentration_layout(level, box.width);

    // ShuffleCards appends one Wild Card when the cell count is odd, and the deck it built
    // holds cells-1 cards in that case.
    while (deck_.size() < slots_.size()) deck_.push_back(pools.wild);
    deck_.resize(slots_.size());
    for (std::size_t i = 0; i < slots_.size(); ++i) slots_[i].card_id = deck_[i];

    faces_.assign(slots_.size(), CardFace::Hidden);
    first_reveal_ = -1;
    lockout_ = 0;
    ticks_left_ = level_.total_ticks();
    score_ = 0;
    matches_ = 0;
    return !slots_.empty();
}

int ConcentrationBoard::pairs_total() const { return static_cast<int>(slots_.size()) / 2; }

int ConcentrationBoard::cards_remaining() const {
    int hidden = 0;
    for (CardFace f : faces_)
        if (f != CardFace::Matched) ++hidden;
    return hidden;
}

bool ConcentrationBoard::won() const {
    if (slots_.empty()) return false;
    return cards_remaining() <= 1;
}

CardFace ConcentrationBoard::face(int index) const {
    if (index < 0 || index >= static_cast<int>(faces_.size())) return CardFace::Hidden;
    return faces_[index];
}

int ConcentrationBoard::card_id(int index) const {
    if (index < 0 || index >= static_cast<int>(slots_.size())) return -1;
    return slots_[index].card_id;
}

const CardDef* ConcentrationBoard::card(int index) const {
    if (pools_ == nullptr) return nullptr;
    return pools_->find(card_id(index));
}

bool ConcentrationBoard::click(int index) {
    if (finished() || lockout_ > 0) return false;
    if (index < 0 || index >= static_cast<int>(faces_.size())) return false;
    if (faces_[index] != CardFace::Hidden) return false;

    faces_[index] = CardFace::Shown;
    if (first_reveal_ < 0) {
        first_reveal_ = index;
        return true;
    }

    const int a = first_reveal_;
    const int b = index;
    first_reveal_ = -1;
    const int id_a = slots_[a].card_id;
    const int id_b = slots_[b].card_id;
    const int wild = pools_ != nullptr ? pools_->wild : -1;
    // Card::DoMatcher: equal ids, or the Wild Card on either side.
    const bool matched = id_a == id_b || (wild >= 0 && (id_a == wild || id_b == wild));

    if (matched) {
        faces_[a] = CardFace::Matched;
        faces_[b] = CardFace::Matched;
        ++matches_;
        const CardDef* def = card(b);
        const int type = def != nullptr ? def->type : 0;
        // The multiplier input is the one value in the scoring path that is not established;
        // a plain pair reveals two cards, which yields the times-two branch.
        score_ += match_score(type, 2);
        return true;
    }

    // Mismatch: Board+452 is armed by DoMatcher and the pair flips back after RevealDuration.
    lockout_ = 30 * constants_.reveal_duration;
    return true;
}

void ConcentrationBoard::reveal_all() {
    for (CardFace& f : faces_)
        if (f == CardFace::Hidden) f = CardFace::Shown;
    first_reveal_ = -1;
    lockout_ = 0;
}

void ConcentrationBoard::tick(int ticks) {
    if (ticks <= 0) return;
    if (level_.timed() && ticks_left_ > 0) {
        ticks_left_ -= ticks;
        if (ticks_left_ < 0) ticks_left_ = 0;
    }
    if (lockout_ > 0) {
        lockout_ -= ticks;
        if (lockout_ <= 0) {
            lockout_ = 0;
            for (CardFace& f : faces_)
                if (f == CardFace::Shown) f = CardFace::Hidden;
        }
    }
}

}  // namespace pple
