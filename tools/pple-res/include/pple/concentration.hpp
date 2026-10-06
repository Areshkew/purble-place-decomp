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

#include <array>
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

// UI\Concentration\CardBacks.xml, which `Board::Init` (0x1000888C4) reads once.  Its loop runs
// exactly four times (`for (i = 0; i < 32; i += 8)`), so the fifth `<Back>` (Star) is never
// loaded.  `Board::Init` copies `/buttonpath` to CardBack+8 and `/progpath` to CardBack+1288
// (0x508):
//
//  * `/buttonpath` is the card BACK.  `Card::InitUI` (0x10007B8D8) calls
//    `Card::InitCardButton(this, *(DWORD *)(Level + 328))` -- Level+0x148 is the level index --
//    and `Card::InitCardButton` (0x10007CB4C) builds Card+0x98 ("CardButton") from
//    `GetXml(*(CardBack[level] + 8))`.  So each <Level> gets its own back art:
//    Shamrock / Heart / Smiley / GumDrop for levels 0..3.  `blankcardbutton.xml` is a *different*
//    node (Card+0xB0, "BlankCard") that the intro state shows instead.
//  * `/progpath` is the progress meter's indicator sprite.
//
// `/imagepath`, `/animpath`, `/moviepath` and `/jarpath`:
//  * `/imagepath` (+264) is the COIN.  `Jar::DoFiller` (0x100083B10) and `ArcBehavior::Init`
//    (0x100083E48) both create a node from it, and `DoFiller` sets its scale to 38 percent
//    (`*((_DWORD *)v14 + 6) = 38`, and NodeSprite+0x18 is the scale).
//  * `/moviepath` (+1032) is the jar's per-level burst movie; `Jar::LoadMovies` (0x1000835D0)
//    reads `(Board+0x20)[i] + 1032` for i = 0..3 into the array at Jar+0x48.
//  * `/animpath` (+520) has no reader in the Concentration code (the two `add ..., 208h` hits in
//    the image are ComfyCakes destructors).
//  * `/jarpath` (+776) has no reader anywhere in the image.
struct CardBackPaths {
    std::string button;  // CardBack+8,      /buttonpath   the card BACK for this level
    std::string image;   // CardBack+264,    /imagepath    the coin
    std::string movie;   // CardBack+1032,   /moviepath    the jar's burst movie
    std::string prog;    // CardBack+1288,   /progpath     the progress meter's indicator
};
std::vector<CardBackPaths> concentration_cardbacks(std::span<const std::uint8_t> cardbacks_xml);

// One `<ImageInstance><Frame num="0" x= y= rotation=/>` of a `*_Points.xml`.  The four shipped
// files (Shamrock/Heart/Smiley/GumDrop_Points.xml) each hold 28 of them.
struct JarPoint {
    int x = 0;
    int y = 0;
    float rotation = 0.0f;  // radians, as Jar::DoFiller stores it (degrees * 0.017453292)
};
std::vector<JarPoint> concentration_jar_points(std::span<const std::uint8_t> points_xml);

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
//   mismatch : the ids differ and neither is the Wild Card -> Board+452 is armed as a lockout
//              and the pair flips back after RevealDuration; Con_TileFlip_Incorrect (proto 0x74)
//   match    : the ids are equal, or exactly one of them is the Wild Card
//   points   : MatchPoints for the card's <type>   (3 and 0 and 6 and everything else -> Normal,
//              4 -> Tricky, 5 -> SuperTricky), times 3 when the multiplier input is <= 1, times 2
//              when it is 2, times 1 above
//   wild     : when exactly ONE of the two is the Wild Card the engine takes the *else* branch:
//              a flat +10 and no MatchMade at all, and the sound is Con_PowerUp (proto 0x72)
//
// The multiplier input is settled: Card::DoMatcher passes `(a[+0x50] + b[+0x50]) / 2`, and
// Card::Init (0x10007B6E4, at 0x10007B717) is the only writer of Card+0x50 in the Concentration
// code -- it stores 0 -- so the input is always 0 and the shipped build always multiplies by 3.
// The x2 and x1 branches are dead code.
// ---------------------------------------------------------------------------------------
enum class CardFace { Hidden, Shown, Matched };

// What one click did, so the caller drives the audio from the engine's own branches.
struct ClickResult {
    bool accepted = false;    // a card was turned over
    bool second = false;      // it was the second card of the pair, so the rules ran
    bool match = false;       // Con_Correct             idA == idB
    bool mismatch = false;    // Con_TileFlip_Incorrect  neither is the Wild Card
    bool wild_match = false;  // Con_PowerUp             exactly one is the Wild Card
    bool chef = false;        // Con_PurbleChef          the Chef's cake swept the board
    bool mixer = false;       // Con_Mixer               Board::DoMixer reshuffled the deck
    bool sneak_get = false;   // Con_SneakPeek_Get       the card carried the sneak-peek icon
    int time_bonus = 0;       // Con_TimeDelay           ticks DoTimeBonus added
    bool board_cleared = false;  // the last pair went in: Con_BoardCleared comes from the jar
    bool level_up = false;    // Board::NewLevel ran: Con_NewScreen and the art must be rebuilt
    bool relaid_out = false;  // the cards moved (the Mixer): the art must be rebuilt
    // Board::DoFlyIntoJar (0x10008AF54) is called from Card::Update (0x10007DA3C, at 0x10007DB01)
    // when a card's state goes 5 -> 2, so every card that became Matched sends one coin.  The
    // caller flies them and only then lets the board end; see end_pending/end_board.
    std::vector<int> flew;
};

// UI\Concentration\RANKS.XML, read by ConRoundData::CalcEndGame (0x100080718).  Its four tables:
//
//   /TimeBonus/timerank[]     floor, ceil, advanced, intermediate  -- picked by the percentage of
//                             the last level's time still on the clock
//   /FlipBonus/fliprank[]     multiplier, advanced, intermediate, beginner -- the first entry
//                             whose multiplier is >= Clicks / (levels * cards)
//   /RankNames/rank[]/title   five titles, indexed by the rank the score lands in
//   /Ranks/<table>/Rank[]     floor, ceil -- the table is chosen by difficulty and timed-ness:
//                             /BeginnerUntimed, /IntermediateTimed, /IntermediateUntimed,
//                             /AdvancedTimed, /AdvancedUnTimed
struct TimeBonusRank {
    int floor = 0;
    int ceil = 0;
    int advanced = 0;
    int intermediate = 0;
};

struct FlipBonusRank {
    float multiplier = 0.0f;
    int advanced = 0;
    int intermediate = 0;
    int beginner = 0;
};

struct ScoreRank {
    int floor = 0;
    int ceil = 0;
};

struct ConcentrationRanks {
    std::vector<TimeBonusRank> time_bonus;
    std::vector<FlipBonusRank> flip_bonus;
    std::vector<std::string> rank_names;
    std::vector<ScoreRank> beginner_untimed;
    std::vector<ScoreRank> intermediate_timed;
    std::vector<ScoreRank> intermediate_untimed;
    std::vector<ScoreRank> advanced_timed;
    std::vector<ScoreRank> advanced_untimed;
};

bool concentration_ranks_parse(std::span<const std::uint8_t> ranks_xml, ConcentrationRanks& ranks,
                               std::string& error);

// ConGeneralData (ToXml 0x10007FC40, FromXml 0x10007FE7C) together with TipManager's
// DisabledTips, which is what ConcentrationGame::SaveGameStats (0x100082C70) hands to
// XmlStash::SaveXMLFile (0x100079A00) for ConcentrationScoresAndSettings.xml.  The arrays are
// all indexed by difficulty: 0 Beginner, 1 Intermediate, 2 Advanced.
//
// The document itself:
//
//   <SettingsData>
//       <GeneralData>
//           <Version>10</Version>            AppendInt;  FromXml refuses anything but 10
//           <Difficulty>0</Difficulty>       AppendUInt
//           <HighScores>                     3 x AppendUIntArray(5)
//           <ScoreDates>                     3 x AppendUInt64Array(5)
//           <GamesPlayed>                    AppendUIntArray(3)
//           <Timed>true</Timed>              AppendBool
//           <GamesWon>                       AppendUIntArray(3)
//           <PercWon>                        AppendUIntArray(3)
//           <TilesMatched>                   AppendUIntArray(3)
//           <TilesTurned>                    AppendUIntArray(3)
//           <TurnAvg>                        AppendUIntArray(3)
//       </GeneralData>
//       <TipManager><DisabledTips><TipID>...</TipID>...</DisabledTips></TipManager>
//   </SettingsData>
//
// Four spaces per level, LF line ends, and a terminating NUL unit: the container's length field
// is `2 * characters + 2`, the BOM plus the text including that NUL.
struct ConSettings {
    int version = 10;
    int difficulty = 0;
    std::array<std::array<std::uint32_t, 5>, 3> high_scores{};
    std::array<std::array<std::uint64_t, 5>, 3> score_dates{};
    std::array<std::uint32_t, 3> games_played{};
    bool timed = true;
    std::array<std::uint32_t, 3> games_won{};
    std::array<std::uint32_t, 3> perc_won{};
    std::array<std::uint32_t, 3> tiles_matched{};
    std::array<std::uint32_t, 3> tiles_turned{};
    std::array<std::uint32_t, 3> turn_avg{};
    std::vector<std::string> disabled_tips;
};

// Accepts the container's payload (BOM + UTF-16LE) or plain text.
bool con_settings_parse(std::span<const std::uint8_t> payload, ConSettings& out, std::string& error);

// The exact bytes the game writes: FF FE, the document as UTF-16LE, and a terminating NUL unit.
std::vector<std::uint8_t> con_settings_payload(const ConSettings& settings);

// The text alone, for a readable diff.
std::string con_settings_xml(const ConSettings& settings);

// OnEnterWonGameState's statistics update (0x100082273 .. 0x10008244C): one more game played and
// won at `difficulty`, the score inserted into that difficulty's high-score table with `date`,
// the tiles and clicks accumulated, and the two averages recomputed.
void con_settings_record_win(ConSettings& settings, int difficulty, int score, std::uint64_t date,
                             std::uint32_t tiles_matched, std::uint32_t tiles_turned);

class ConcentrationBoard {
public:
    // `levels` is the difficulty's whole <Levels> list, in document order.  `start` picks the
    // entry to deal.  Board::NewLevel (0x10008AAD4) advances the Level pointer along
    // Level+0x110 and increments Round+0x124 once the board is cleared and more levels remain,
    // so Intermediate plays two boards and Advanced four.
    bool reset(const std::vector<ConcentrationLevel>& levels, std::size_t start,
               const CardPools& pools, const ConcentrationConstants& constants, const CardsBox& box,
               unsigned seed);

    const ConcentrationLevel& level() const { return levels_[level_pos_]; }
    const std::vector<ConcentrationSlot>& slots() const { return slots_; }
    const std::vector<int>& deck() const { return deck_; }

    int columns() const { return level().num_cols; }
    int rows() const { return level().num_rows; }
    int cell_count() const { return static_cast<int>(slots_.size()); }

    CardFace face(int index) const;
    int card_id(int index) const;
    // The id of the drawn card, as the pools give it.
    const CardDef* card(int index) const;

    int pending_reveal() const { return first_reveal_; }
    int lockout_ticks() const { return lockout_; }
    float ticks_remaining() const { return static_cast<float>(ticks_left_); }
    float seconds_remaining() const { return static_cast<float>(ticks_left_) / 30.0f; }
    bool timed() const { return level().timed(); }
    int score() const { return score_; }
    int matches() const { return matches_; }
    // Round+0x12C: Board::Update (0x10008A4F0) counts the cards in state 2 here, and Jar::DoFiller
    // (0x100083B10) fills the jar from it.  Board::InitLevel zeroes it per level, but Jar::InitUI
    // (0x100083218, at 0x100083593) only zeroes the jar's own coin count while the coin nodes it
    // already made stay in place, so the shipped pile never shrinks.  `cards_removed_total` is the
    // round's running total, which is the same visible behaviour without the duplicate nodes.
    int cards_removed() const;
    int cards_removed_total() const { return cards_removed_total_; }
    int round_cards() const;
    int cards_remaining() const;
    int pairs_total() const;
    // The whole round, i.e. every <Level> of this difficulty: Round+0x120 accumulates across a
    // Board::NewLevel, so a two-level Intermediate round matches 36 pairs, not 18.
    int round_pairs_total() const;
    // Round+292 / Round+0x124, the field ProgressMeter::InitUI (0x100084720) lights indicators
    // from, and the number of <Level> entries this difficulty has.  Beginner 1, Intermediate 2,
    // Advanced 4 -- the meter has one indicator per level and lights them as they are cleared.
    int level_position() const { return static_cast<int>(level_pos_); }
    int level_count() const { return static_cast<int>(levels_.size()); }
    // Round+284 / Round+0x11C, from Level+0x30 = <NumStartTokens>.
    int tokens() const { return tokens_; }

    // Board::Update (0x10008A4F0): the round is complete when
    // `NumCards - matched <= count(id == WildCard || id == MixerCard)`.  A fresh game starts with
    // Round+0x1D4 = 0 and Board::InitLevel sets it to 1; the deck's own Wild/Mixer cards are what
    // it counts, so an odd board's lone Wild Card is the "<= 1".
    bool won() const;
    bool lost() const { return timed() && ticks_left_ <= 0 && !won(); }
    bool finished() const { return won() || lost(); }

    // A completed board does not end the round by itself.  Board::Update's completion pass leaves
    // it complete, Jar::Update (0x10008373C) then plays Con_BoardCleared and shows the level's
    // burst movie (CardBacks.xml's /moviepath, Jar::LoadMovies 0x1000835D0), and only when that
    // movie has finished does Jar::StopExplosion (0x100083A20) decide:
    //
    //     no next <Level>, or Round+0x124 >= Difficulty+0x20  ->  OnEnterState(2), the win
    //     otherwise                                            ->  Board::NewLevel
    //
    // `end_pending` is that gap.  The caller flies the coins and runs the burst, then calls
    // `end_board`, which is StopExplosion.  The loss is separate: Board::Update tests
    // `Level+0x0C > 0 && Round+0x154 != 0 && Round+0x118 <= 0` and goes straight to
    // OnEnterState(3), with no jar involved, which `tick` records as `round_over`.
    bool end_pending() const { return end_pending_; }
    ClickResult end_board();
    bool round_over() const { return round_over_; }
    bool round_won() const { return round_over_ && won(); }

    // ConRoundData::CalcEndGame (0x100080718), the second scoring stage that runs at the end of a
    // won round.  It is NOT the number the game's own status bar shows
    // (ConcentrationGame::UpdateStatusBar 0x100082D68 shows Round+0x120, "Score"): this is
    // Points + TimeBonus + FlipBonus, and the rank that lands in.
    //
    //   TimeBonus = /TimeBonus's <intermediate|advanced> for the timerank whose
    //               [floor, ceil) contains (int)(BonusTime / TotalGameTime * 100 + 0.05),
    //               where TotalGameTime is the CURRENT level's budget and BonusTime the time left
    //               on it (OnEnterWonGameState 0x10008222B adds `Level+0x0C` and `Round+0x118`).
    //               Gated on the difficulty being non-Beginner, as the code is.
    //   FlipBonus = /FlipBonus's column for the first fliprank whose <multiplier> is >=
    //               Clicks / (levels * NumCards), where Clicks is Round+0x128 -- one per
    //               Card::FlipForward (0x10007D017), so the completion pass and a sneak peek both
    //               count against it.
    //   Rank      = the last /Ranks/<table> entry whose <floor> the total reaches, and the title
    //               is /RankNames/rank[that index]/title.
    struct EndGame {
        int points = 0;
        int time_bonus = 0;
        int flip_bonus = 0;
        int total = 0;
        int rank = -1;
        std::string title;
        int time_percent = 0;
        double flip_ratio = 0.0;
    };
    EndGame calc_end_game(const ConcentrationRanks& ranks) const;
    // Round+0x128: Card::FlipForward increments it for every card turned face up.
    int clicks() const { return clicks_; }

    // Reveals card `index`.  Returns false when the click cannot do anything.
    ClickResult click(int index);

    // Board::SneakPeaker (0x10008B280): spends one <NumStartTokens> token and turns the whole
    // board face up for Board+0x194 ticks, then flips those cards back (Board::Update).
    // Returns false when there is no token left or a peek is already running.
    bool use_token();

    // Inspection helper: turns every face card up without scoring it.
    void reveal_all();

    // Advances the reveal timer, the sneak-peek timer and the countdown.  30 ticks make a second.
    void tick(int ticks = 1);

    // Score for one match, exactly as Board::MatchMade computes it.
    int match_score(int card_type, int multiplier_input) const;

private:
    void deal(std::size_t position, unsigned seed);
    void force_match_remaining(std::vector<int>* flew);

    std::vector<ConcentrationLevel> levels_;
    std::size_t level_pos_ = 0;
    ConcentrationConstants constants_;
    const CardPools* pools_ = nullptr;
    CardsBox box_;
    unsigned seed_ = 0;
    std::vector<ConcentrationSlot> slots_;
    std::vector<int> deck_;
    std::vector<bool> sneak_icon_;  // Card+0x18: the sneak-peek mini icon is on this card
    std::vector<bool> sneak_shown_;  // Card+0x46: this card was turned by a sneak peek
    std::vector<CardFace> faces_;
    int first_reveal_ = -1;
    int lockout_ = 0;
    // The mismatch countdown does not start at the click: Card::DoMatcher runs from
    // Card::OnReveal (0x10007FA6B), i.e. when the second card's flip-forward finishes.  The
    // reconstruction's flip is 17 percent a tick from 100 down to 15 and back, so 10 ticks.
    int lockout_delay_ = 0;
    int ticks_left_ = 0;
    int score_ = 0;
    int matches_ = 0;
    int cards_removed_total_ = 0;
    int tokens_ = 0;
    int sneak_ticks_ = 0;
    int clicks_ = 0;
    bool end_pending_ = false;
    bool round_over_ = false;
};


}  // namespace pple
