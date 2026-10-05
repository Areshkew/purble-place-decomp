// Purble Pairs (the Concentration game): data and rules, no SDL.
//
// Everything here is a reading of the executable:
//
//   Board::Init            0x1000888C4  loads the deck and the three difficulties
//   Difficulty::Init       0x10008BA7C  <DifficultyModes> -> per-difficulty and per-level fields
//   Board::XMLConstants    0x100089E7C  <Constants>
//   Board::BuildCardLists  0x100088FE0  CARDS.XML -> pools keyed by <type>
//   Board::GetCardList     0x100089AAC  <CardPairs> -> the list of card ids
//   Board::ShuffleCards    0x100089604  cell size, row-major placement, card objects
//   Card::DoMatcher        0x10007D224  the match rule
//   Board::MatchMade       0x10008B0AC  scoring
//
// Two details worth stating up front because they are easy to get wrong:
//
//  * A card is NOT a sheet frame chosen by the letter in <CardPairs>.  CARDS.XML names a
//    sprite node definition per card (<path>), so the face art is an ordinary
//    <Root Type="Sprite"> frame.
//  * <CardPairs> carries one character per PAIR of cards, over NumCols*NumRows cells; an odd
//    cell count drops the last cell and then gets one extra Wild Card appended, so an odd
//    board has a single card that can never match.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pple {

// One <Card> in UI\Concentration\Cards.xml.
struct CardDef {
    int id = -1;
    int type = 0;  // Card::CardType; drives both the pool and the score value
    bool chef = false;
    std::string path;  // sprite node definition for the face
    std::string desc;  // localisation string "|id|context|text"
};

// The pools Board::BuildCardLists fills.  A <type> with exactly one entry becomes a single id
// rather than a list, which is why the five special cards are scalars here.
struct CardPools {
    std::vector<CardDef> cards;
    std::vector<int> normal;        // <type> 0, 21 entries in the shipped file
    std::vector<int> tricky;        // <type> 4
    std::vector<int> super_tricky;  // <type> 5
    int chef = -1;                  // <type> 1, 'P'  (Board+396)
    int mixer = -1;                 // <type> 3, 'M'  (Board+372)
    int time_bonus = -1;            // <type> 6, 'T'  (Board+376)
    int unassigned = -1;            // <type> 7       (Board+380)
    int wild = -1;                  // <type> 8, 'J'  (Board+384)

    const CardDef* find(int id) const;
};

// <Constants>, at Board+408..464.
struct ConcentrationConstants {
    int happy_chef_duration = 60;
    int sneak_match_bonus = 0;
    int sneak_peak_icon_freq_seconds = 0;  // stored as 30 * the XML value
    int sneak_peak_icon_duration_seconds = 0;
    int board_border_width = 0;
    int board_border_height = 0;
    int match_points_normal = 0;
    int match_points_happy_chef = 0;  // read, but not used by MatchMade
    int match_points_sad_chef = 0;    // read, but not used by MatchMade
    int match_points_blender = 0;     // read, but not used by MatchMade
    int match_points_tricky = 0;
    int match_points_super_tricky = 0;
    int reveal_duration = 0;         // seconds a mismatched pair stays face up
    int sneak_reveal_duration = 0;
    int time_bonus_percent = 0;
    int sneak_time_decrement = 0;
};

// One <Level> inside a difficulty.
struct ConcentrationLevel {
    std::string difficulty;  // "Beginner", "Intermediate", "Advanced"
    int difficulty_index = 0;
    int level_index = 0;

    int num_start_tokens = 0;
    int num_cols = 0;
    int num_rows = 0;
    int x_spacing = 0;
    int y_spacing = 0;
    int num_tricky = 0;
    int num_trickier = 0;

    int total_time_seconds = 0;
    int num_card_types = 0;
    int num_unique_pairs = 0;
    std::string card_pairs;

    int cell_count() const { return num_cols * num_rows; }
    // Level+12 = 30 * TotalTime, i.e. a tick count at the engine's fixed 30 Hz.  Beginner has
    // no <TotalTime> at all, so the engine stores 30 * -1; a non-positive value is an untimed
    // round (the "timed" flag the Professor's tip strings branch on, Round+340, comes from
    // elsewhere again).
    int total_ticks() const { return 30 * total_time_seconds; }
    bool timed() const { return total_time_seconds > 0; }
};

// The scene document's <Cards><Width>120</Width><Height>120</Height></Cards>.
struct CardsBox {
    int width = 120;
    int height = 120;
};

bool concentration_pools_parse(std::span<const std::uint8_t> cards_xml, CardPools& pools,
                              std::string& error);
bool concentration_game_parse(std::span<const std::uint8_t> scene_xml, ConcentrationConstants& constants,
                             std::vector<ConcentrationLevel>& levels, std::string& error);
bool concentration_cards_box_parse(std::span<const std::uint8_t> scene_xml, CardsBox& box,
                                   std::string& error);

// One placed card.  Indices, columns and rows are the engine's own (Card+28, +56, +52).
struct ConcentrationSlot {
    int index = 0;
    int col = 0;
    int row = 0;
    int x = 0;  // inside the PlayArea node's space
    int y = 0;
    int w = 0;  // cell size, Card+40 / Card+44
    int h = 0;
    int scale_percent = 100;  // Card+48
    int card_id = -1;
};

// Board::GetCardList + ShuffleCards.  `seed` feeds a std::mt19937 standing in for rand(); the
// engine's own shuffle is not reproducible from the file, and its bookkeeping is noted in the
// report.  The deck contents are otherwise exactly as the engine builds them.
std::vector<int> concentration_build_deck(const ConcentrationLevel& level, const CardPools& pools,
                                         unsigned seed);

// Board::ShuffleCards' placement arithmetic, verbatim.
std::vector<ConcentrationSlot> concentration_layout(const ConcentrationLevel& level, int cards_width);

// ---------------------------------------------------------------------------------------
// The playable board.  Rules as read from Card::DoMatcher (0x10007D224) and Board::MatchMade
// (0x10008B0AC):
//
//   match  : equal <id>, or either card is the Wild Card
//   points : MatchPoints for the card's <type>   (3 and 0 and 6 and everything else -> Normal,
//            4 -> Tricky, 5 -> SuperTricky)
//            times 3 when the multiplier input is <= 1, times 2 when it is 2, times 1 above
//   mismatch: Board+452 is armed as a lockout and the pair flips back after RevealDuration
// ---------------------------------------------------------------------------------------
enum class CardFace { Hidden, Shown, Matched };

class ConcentrationBoard {
public:
    bool reset(const ConcentrationLevel& level, const CardPools& pools,
               const ConcentrationConstants& constants, const CardsBox& box, unsigned seed);

    const ConcentrationLevel& level() const { return level_; }
    const std::vector<ConcentrationSlot>& slots() const { return slots_; }
    const std::vector<int>& deck() const { return deck_; }

    int columns() const { return level_.num_cols; }
    int rows() const { return level_.num_rows; }
    int cell_count() const { return static_cast<int>(slots_.size()); }

    CardFace face(int index) const;
    int card_id(int index) const;
    // The id of the drawn card, as the pools give it.
    const CardDef* card(int index) const;

    int pending_reveal() const { return first_reveal_; }
    int lockout_ticks() const { return lockout_; }
    float ticks_remaining() const { return static_cast<float>(ticks_left_); }
    float seconds_remaining() const { return static_cast<float>(ticks_left_) / 30.0f; }
    bool timed() const { return level_.timed(); }
    int score() const { return score_; }
    int matches() const { return matches_; }
    int cards_remaining() const;
    int pairs_total() const;
    // An odd board always leaves one card over: NumCols*NumRows loses its last cell in
    // Board::GetCardList and ShuffleCards then appends a single Wild Card for it.  The round is
    // therefore complete when at most one card is still unmatched.  The engine's own test sits
    // in Board::Update (0x10008A4F0), which I did not read; this is the behaviour the deck
    // shape forces.
    bool won() const;
    bool lost() const { return timed() && ticks_left_ <= 0 && !won(); }
    bool finished() const { return won() || lost(); }

    // Reveals card `index`.  Returns false when the click cannot do anything.
    bool click(int index);

    // Inspection helper: turns every face card up without scoring it.
    void reveal_all();

    // Advances the reveal timer and the countdown.  30 ticks make a second.
    void tick(int ticks = 1);

    // Score for one match, exactly as Board::MatchMade computes it.
    int match_score(int card_type, int multiplier_input) const;

private:
    ConcentrationLevel level_;
    ConcentrationConstants constants_;
    const CardPools* pools_ = nullptr;
    std::vector<ConcentrationSlot> slots_;
    std::vector<int> deck_;
    std::vector<CardFace> faces_;
    int first_reveal_ = -1;
    int lockout_ = 0;
    int ticks_left_ = 0;
    int score_ = 0;
    int matches_ = 0;
};

}  // namespace pple
