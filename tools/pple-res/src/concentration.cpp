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

bool ConcentrationBoard::reset(const std::vector<ConcentrationLevel>& levels, std::size_t start,
                              const CardPools& pools, const ConcentrationConstants& constants,
                              const CardsBox& box, unsigned seed) {
    if (levels.empty()) return false;
    levels_ = levels;
    level_pos_ = std::min(start, levels.size() - 1);
    constants_ = constants;
    pools_ = &pools;
    box_ = box;
    seed_ = seed;
    score_ = 0;
    matches_ = 0;
    cards_removed_total_ = 0;
    deal(level_pos_, seed);
    return !slots_.empty();
}

// Board::InitLevel (0x1000894E0) + Board::ShuffleCards (0x100089604) + Board::SelectSneakCard
// (0x10008B348) for one <Level> entry: the grid, the shuffled deck, the sneak-peek icon and the
// tick budget.
void ConcentrationBoard::deal(std::size_t position, unsigned seed) {
    level_pos_ = position;
    const ConcentrationLevel& lv = levels_[level_pos_];
    deck_ = concentration_build_deck(lv, *pools_, seed);
    slots_ = concentration_layout(lv, box_.width);

    // ShuffleCards appends one Wild Card when the cell count is odd, and the deck it built
    // holds cells-1 cards in that case.
    while (deck_.size() < slots_.size()) deck_.push_back(pools_->wild);
    deck_.resize(slots_.size());
    for (std::size_t i = 0; i < slots_.size(); ++i) slots_[i].card_id = deck_[i];

    faces_.assign(slots_.size(), CardFace::Hidden);
    sneak_icon_.assign(slots_.size(), false);
    sneak_shown_.assign(slots_.size(), false);
    first_reveal_ = -1;
    lockout_ = 0;
    sneak_ticks_ = 0;
    // Board::InitGame: Round+280 (TimeLeft) = Level+12, Round+292 (Level) = 0 on a new game and
    // Round+284 (Tokens) = Level+48 = <NumStartTokens>.
    ticks_left_ = lv.total_ticks();
    tokens_ = lv.num_start_tokens;
    // Board::SelectSneakCard picks one card that is not already matched (state != 2) at random
    // and sets Card+0x18 on it, so a match that includes it pays <SneakMatchBonus>.
    if (!slots_.empty()) {
        std::mt19937 rng(seed ^ 0x5eec5eedu);
        sneak_icon_[rng() % slots_.size()] = true;
    }
}

int ConcentrationBoard::pairs_total() const { return static_cast<int>(slots_.size()) / 2; }

int ConcentrationBoard::round_pairs_total() const {
    int total = 0;
    for (const ConcentrationLevel& lv : levels_) total += lv.cell_count() / 2;
    return total;
}

int ConcentrationBoard::cards_remaining() const {
    int hidden = 0;
    for (CardFace f : faces_)
        if (f != CardFace::Matched) ++hidden;
    return hidden;
}

// Board::Update's `++Round+0x12C` for every card whose state is 2.
int ConcentrationBoard::cards_removed() const {
    int gone = 0;
    for (CardFace f : faces_)
        if (f == CardFace::Matched) ++gone;
    return gone;
}

int ConcentrationBoard::round_cards() const {
    int total = 0;
    for (const ConcentrationLevel& lv : levels_) total += lv.cell_count();
    return total;
}

// Board::Update (0x10008A4F0): the board is complete when the cards still on it are only the
// Wild and Mixer cards -- `NumCards - matched <= Board+0x1D4`, and Board+0x1D4 counts the cards
// whose id is Board+0x180 (the Wild Card) or Board+0x174 (the Mixer).  Then every remaining card
// is force-matched (Card+0x45) and flipped forward, which is how the lone Wild Card of an odd
// board is cleared.
bool ConcentrationBoard::won() const {
    if (slots_.empty()) return false;
    int spared = 0;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (faces_[i] == CardFace::Matched) continue;
        const int id = slots_[i].card_id;
        if (pools_ != nullptr && (id == pools_->wild || id == pools_->mixer)) ++spared;
    }
    return cards_remaining() <= spared;
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

void ConcentrationBoard::force_match_remaining() {
    // Board::Update's completion pass: every card still in state 0 is force-matched and flipped.
    for (std::size_t i = 0; i < faces_.size(); ++i) {
        if (faces_[i] == CardFace::Matched) continue;
        faces_[i] = CardFace::Matched;
        ++matches_;
        ++cards_removed_total_;
        const CardDef* def = card(static_cast<int>(i));
        score_ += match_score(def != nullptr ? def->type : 0, 0);
    }
}

bool ConcentrationBoard::use_token() {
    if (tokens_ <= 0 || sneak_ticks_ > 0 || slots_.empty()) return false;
    --tokens_;
    // Board::SneakPeaker turns every card whose state is 0 or 4 face up and flags it with
    // Card+0x46; Board::Update flips those back when Board+0x194 reaches zero.
    const int hold = 30 * constants_.sneak_peak_icon_duration_seconds;
    sneak_ticks_ = hold > 0 ? hold : 600;
    for (std::size_t i = 0; i < faces_.size(); ++i) {
        if (faces_[i] == CardFace::Matched) continue;
        faces_[i] = CardFace::Shown;
        sneak_shown_[i] = true;
    }
    first_reveal_ = -1;
    return true;
}

ClickResult ConcentrationBoard::click(int index) {
    ClickResult result;
    if (finished() || lockout_ > 0 || sneak_ticks_ > 0) return result;
    if (index < 0 || index >= static_cast<int>(faces_.size())) return result;
    if (faces_[index] != CardFace::Hidden) return result;

    faces_[index] = CardFace::Shown;
    result.accepted = true;
    if (first_reveal_ < 0) {
        first_reveal_ = index;
        return result;
    }

    const int a = first_reveal_;
    const int b = index;
    first_reveal_ = -1;
    const int id_a = slots_[a].card_id;
    const int id_b = slots_[b].card_id;
    const int wild = pools_ != nullptr ? pools_->wild : -1;
    const bool wild_a = wild >= 0 && id_a == wild;
    const bool wild_b = wild >= 0 && id_b == wild;
    result.second = true;

    // Card::DoMatcher's first test: the ids differ and neither is the Wild Card.
    if (id_a != id_b && !wild_a && !wild_b) {
        lockout_ = 30 * constants_.reveal_duration;
        result.mismatch = true;
        return result;
    }

    faces_[a] = CardFace::Matched;
    faces_[b] = CardFace::Matched;
    ++matches_;
    cards_removed_total_ += 2;

    // The Chef's cake sweep.  Card::DoMatcher calls Board::GetChefCakeID (0x100089D00) and, when
    // either card is that id, forces every card with the id to match and scores each pair.
    const int chef = pools_ != nullptr ? pools_->chef : -1;
    if (chef >= 0 && (id_a == chef || id_b == chef)) {
        for (std::size_t i = 0; i < faces_.size(); ++i) {
            if (faces_[i] == CardFace::Matched) continue;
            if (slots_[i].card_id != chef) continue;
            faces_[i] = CardFace::Matched;
            ++matches_;
            ++cards_removed_total_;
            const CardDef* def = card(static_cast<int>(i));
            score_ += match_score(def != nullptr ? def->type : 0, 0);
        }
        result.chef = true;
    }

    // The sneak-peek icon bonus: `*((_BYTE *)this + 24) != 0 || v83 != 0` -- either card
    // carries it.  Card::PlayBonusAnim, SneakPeakArea::AddPeak and <SneakMatchBonus>.
    const bool icon_a = a < static_cast<int>(sneak_icon_.size()) && sneak_icon_[a];
    const bool icon_b = b < static_cast<int>(sneak_icon_.size()) && sneak_icon_[b];
    if (icon_a || icon_b) {
        score_ += constants_.sneak_match_bonus;
        result.sneak_get = true;
    }

    if (wild_a != wild_b) {
        // Exactly one is the Wild Card: Card::DoMatcher's else branch -- a flat +10 and no
        // Board::MatchMade, so the match counter and the type table do not apply.
        score_ += 10;
        result.wild_match = true;
    } else {
        // Card::DoMixerCheck (0x10007D9DC) on both cards: the Mixer reshuffles the cards that
        // are still on the board.  Its gate is `card id == Board+0x174` plus
        // `NumCards - matched > 3`.
        const int mixer = pools_ != nullptr ? pools_->mixer : -1;
        if (mixer >= 0 && (id_a == mixer || id_b == mixer) &&
            static_cast<int>(slots_.size()) - cards_remaining() > 3) {
            // Board::DoMixer (0x100089870): Fisher-Yates over the live cards, then re-place them.
            std::vector<std::size_t> live;
            for (std::size_t i = 0; i < faces_.size(); ++i)
                if (faces_[i] != CardFace::Matched) live.push_back(i);
            std::mt19937 rng(seed_ + static_cast<unsigned>(matches_));
            for (std::size_t i = live.size(); i-- > 1;) {
                std::uniform_int_distribution<std::size_t> pick(0, i);
                std::swap(slots_[live[i]].card_id, slots_[live[pick(rng)]].card_id);
            }
            for (std::size_t i = 0; i < deck_.size(); ++i) deck_[i] = slots_[i].card_id;
            result.mixer = true;
            result.relaid_out = true;
        }
        const CardDef* def = card(b);
        const int type = def != nullptr ? def->type : 0;
        // Card::DoMatcher passes `(this[+0x50] + other[+0x50]) / 2`, and Card::Init stores 0 in
        // both, so the multiplier input is always 0.
        score_ += match_score(type, 0);
        result.match = true;
    }

    // The Time Bonus card pays ticks: Board::DoTimeBonus (0x10008B194).
    const int bonus_card = pools_ != nullptr ? pools_->time_bonus : -1;
    if (bonus_card >= 0 && (id_a == bonus_card || id_b == bonus_card)) {
        const int total = level().total_ticks();
        const int add = static_cast<int>(static_cast<double>(constants_.time_bonus_percent) / 200.0 *
                                        static_cast<double>(total));
        if (ticks_left_ + add < total) {
            ticks_left_ += add;
        } else {
            // The engine's own clamp, `Level+12 + 1`, which makes TimeBar::CalculateTimer's
            // (total - remaining) negative -- the bar goes empty.
            ticks_left_ = total + 1;
        }
        result.time_bonus = add;
    }

    if (won()) {
        // Board::Update's completion pass, then Jar::StopExplosion's decision: with no next
        // <Level> the game is won, otherwise Board::NewLevel advances Round+0x124.
        force_match_remaining();
        if (level_pos_ + 1 < levels_.size()) {
            ++level_pos_;
            result.level_up = true;
            result.relaid_out = true;
            const int kept_score = score_;
            const int kept_matches = matches_;
            deal(level_pos_, seed_ + static_cast<unsigned>(level_pos_));
            score_ = kept_score;
            matches_ = kept_matches;
        } else {
            result.board_cleared = true;
        }
    }
    return result;
}

void ConcentrationBoard::reveal_all() {
    for (CardFace& f : faces_)
        if (f == CardFace::Hidden) f = CardFace::Shown;
    first_reveal_ = -1;
    lockout_ = 0;
}

void ConcentrationBoard::tick(int ticks) {
    if (ticks <= 0) return;
    if (level().timed() && ticks_left_ > 0) {
        ticks_left_ -= ticks;
        if (ticks_left_ < 0) ticks_left_ = 0;
    }
    if (lockout_ > 0) {
        lockout_ -= ticks;
        if (lockout_ <= 0) {
            lockout_ = 0;
            for (std::size_t i = 0; i < faces_.size(); ++i)
                if (faces_[i] == CardFace::Shown) faces_[i] = CardFace::Hidden;
        }
    }
    if (sneak_ticks_ > 0) {
        sneak_ticks_ -= ticks;
        if (sneak_ticks_ <= 0) {
            sneak_ticks_ = 0;
            for (std::size_t i = 0; i < faces_.size(); ++i) {
                if (!sneak_shown_[i]) continue;
                sneak_shown_[i] = false;
                if (faces_[i] == CardFace::Shown) faces_[i] = CardFace::Hidden;
            }
        }
    }
}

std::vector<CardBackPaths> concentration_cardbacks(std::span<const std::uint8_t> cardbacks_xml) {
    std::vector<CardBackPaths> out;
    XmlNode root;
    std::string error;
    if (!xml_parse(cardbacks_xml, root, error)) return out;
    for (const XmlNode& back : root.children) {
        if (lower(back.name) != "back") continue;
        // Board::Init's loop is four iterations of an unconditional 8-byte stride.
        if (out.size() == 4) break;
        CardBackPaths paths;
        paths.button = back.child_text("buttonpath");
        paths.image = back.child_text("imagepath");
        paths.movie = back.child_text("moviepath");
        paths.prog = back.child_text("progpath");
        out.push_back(std::move(paths));
    }
    return out;
}

// Jar::DoFiller walks the root's children and reads `/@x`, `/@y` and `/@rotation` off each one's
// first child -- which in these files is the `<Frame num="0" .../>` inside an `<ImageInstance>`.
std::vector<JarPoint> concentration_jar_points(std::span<const std::uint8_t> points_xml) {
    std::vector<JarPoint> out;
    XmlNode root;
    std::string error;
    if (!xml_parse(points_xml, root, error)) return out;
    for (const XmlNode& instance : root.children) {
        if (instance.children.empty()) continue;
        const XmlNode& frame = instance.children.front();
        JarPoint p;
        p.x = std::atoi(frame.attr("x", "0").c_str());
        p.y = std::atoi(frame.attr("y", "0").c_str());
        p.rotation = static_cast<float>(std::atof(frame.attr("rotation", "0").c_str()) * 0.017453292);
        out.push_back(p);
    }
    return out;
}

}  // namespace pple

