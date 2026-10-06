#pragma once
#include "types.hpp"

namespace pple::viewer {

// Owns the former main() state. Methods are grouped by responsibility in .cpp files.
// SDL object lifetime and the resource-fetch adapter remain explicit in run()/run_loop().
class App {
public:
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;
    int run(int argc, char** argv);
private:
    int run_loop();
    double refresh_total_ms = 0.0;
    double refresh_first_ms = 0.0;
    double build_ms = 0.0;
    TTF_Font* font_for(float size);
    void rebuild_hud();
    void build_coverage(SDL_Surface* surface, std::vector<std::uint8_t>& grid,                                   bool alpha_only);
    float rect_coverage(const std::string& sheet, const SDL_FRect& src);
    SDL_Texture* load_sheet_texture(const std::string& requested, const std::string& resolved_name,                                        std::string& note);
    void release_draws();
    void build_movie();
    bool read_resource(const std::string& name, std::vector<std::uint8_t>& out);
    void build_scene();
    const BurstMovie* load_burst(const std::string& name);
    void refresh_hud();
    void jar_begin_burst();
    void finish_board_end();
    int append_movie(const pple::Movie& mv, int movie_frame, float ax, float ay,
                                  std::vector<DrawItem>& out, float& extent_w,
                                  float& extent_h);
    void spawn_arcs(const std::vector<int>& cells);
    void jar_grow();
    void arm_match_fx(const std::vector<int>& cells, bool immediate);
    void report_round_end();
    void build_pairs();
    void build_pair_level_art(const std::vector<std::uint8_t>& scene_bytes, std::string& scene_how);
    int build_pair_card_assets();
    void build_pair_hud(std::string& scene_how);
    bool load_professor(const std::string& instance);
    void refresh_cards();
    void start_flip(int index, bool to_face);
    void sync_flips();
    bool advance_flips();
    bool advance_card_fx();
    void timed_refresh();
    void load_menu_clouds();
    bool append_label(const pple::SceneItem& item);
    void build_loading();
    void build_menu();
    void load_current(bool announce);
    bool click_scene(float sx, float sy, bool motion_only);
    // ---- tips, the Professor's state machine and the in-canvas dialogs (tips.cpp,
    // professor.cpp, overlays.cpp) -------------------------------------------------------
    TTF_Font* face_font(const std::string& face, float size);
    std::string mui_text(int id);
    void measure_text(const std::string& face, float size, const std::string& text, float wrap,
                      int& w, int& h);
    void append_text(std::vector<DrawItem>& out, const std::string& face, float size,
                     std::uint32_t colour, const std::string& text, float x, float y, float wrap);
    void load_tips();
    bool load_tip_style();
    bool load_tip_slice(TipSlice& slice, const std::string& xml_name, int state);
    const TipDef* find_tip(const std::string& id) const;
    void show_tip(const std::string& id, float x, float y);
    void append_panel(std::vector<DrawItem>& out, float x, float y, float w, float h);
    void append_button(std::vector<DrawItem>& out, const UiButton& button, float label_size);
    void hide_tip();
    void release_overlay();
    void rebuild_overlay();
    void free_direct(std::vector<DrawItem>& out);
    bool load_professor_movie(const std::string& instance);
    void professor_show(const std::string& instance, ProfPhase phase, bool loop);
    void professor_display_tip();
    void professor_tip_closed();
    void professor_level_tip(const std::string& id);
    void professor_advance();
    void professor_round_end(bool won);
    void professor_intro_begin();
    void build_result_panel(bool won);
    void build_difficulty();
    void draw_difficulty();
    void start_game(const std::string& difficulty, bool intro);
    void game_to_menu();

    std::vector<std::string> modules;
    std::string want_resource;
    std::string sprite_xml;
    std::string scene_xml;
    std::string movie_xml;
    std::string movie_name;
    std::string pairs_spec;
    std::string mui_path;
    std::string play_name;
    float play_volume = 1.0f;
    bool mute = false;
    std::string pairs_click;
    std::string shot_path;
    std::string font_path = "C:\\Windows\\Fonts\\arial.ttf";
    std::string save_path;
    int width = 1400, height = 900;
    std::size_t frame = 0;
    int state = 0;
    float anchor_x = 0.0f, anchor_y = 0.0f;
    unsigned seed_for_pairs = 20261005u;
    bool pairs_reveal = false;
    bool list_only = false, include_all = false;
    bool menu_mode = false;
    bool mce_mode = false;
    bool difficulty_mode = false;
    bool tips_enabled = true;
    bool container_offsets = true;
    std::string script_click_spec;
    int pairs_ticks = 0;
    bool pairs_solve = false;
    int shot_delay_ms = 0;
    int bench_frames = 0;
    int bench_build = 0;
    bool loading_mode = false;
    int loading_ms = 1200;
    Uint64 loading_start = 0;
    std::string pairs_save_path;
    // Load the .mui first (if asked), so every label resolved afterwards uses it.
    pple::MuiStrings mui_strings;
    Bundle bundle;
    std::vector<Item> items;
    std::size_t skipped = 0;
    std::size_t index = 0;
    bool offscreen = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    Decoded current;
    SDL_Texture* texture = nullptr;
    SDL_FRect src_rect{
    };
    std::vector<std::string> hud_lines;
    std::vector<SDL_Texture*> hud_textures;
    int hud_w = 0, hud_h = 0;
    bool integer_scale = false;
    std::vector<DrawItem> scene_draws;
    float scene_w = 0.0f, scene_h = 0.0f;
    // Set by the render loop, read back by the click handler to invert scene -> window.
    float view_scale = 1.0f, view_ox = 0.0f, view_oy = 0.0f;
    std::map<int, TTF_Font*> font_cache;
    // Sheet textures, cached by resolved resource name; a scene reuses them heavily.
    // Alongside each texture a coarse 64x64 occupancy grid is kept, so a declared frame
    // rectangle can be checked for actually containing art.
    std::map<std::string, SDL_Texture*> sheet_cache;
    std::map<std::string, std::pair<int, int>> sheet_sizes;
    std::map<std::string, std::vector<std::uint8_t>> sheet_coverage;
    // Resource names the last sheet load went through, for the HUD and the report.
    std::vector<std::string> named_steps;
    std::function<bool(const std::string&, std::vector<std::uint8_t>&)> fetch_resource;
    std::optional<pple::Scene> loaded_scene;
    // Instances build_scene must not draw.  The Concentration document carries three time-bar
    // elements that are not draws: <TimeBarGreen>/<TimeBarRed> are the segment templates the
    // engine clones 226 times (notes 17.7), and <TimeBarBkg> is hidden outright on an untimed
    // difficulty (TimeBar::InitUI 0x10008538E).
    std::set<std::string> hidden_instances;
    // A sprite drawn behind everything the document describes.  The Concentration room is not in
    // the scene document: ConcentrationGame::LoadGameUI (0x100081042) creates
    // UI\Concentration\hearts.xml itself as a child of the game's parent node, so it lands at
    // 0,0 and is Background.png, 800x496.
    std::string scene_background_xml;
    // The index in scene_draws of the /JarFg sprite, so the jar's tray coins can be inserted
    // between /JarBkg and it.  Set by build_scene.
    int pairs_jar_front_at = -1;
    int pairs_jar_base_at = -1;
    // ---- Purble Pairs -----------------------------------------------------------------
    // The board data and rules live in pple::ConcentrationBoard; this only places the cards.
    pple::ConcentrationBoard pairs_board;
    bool pairs_ready = false;
    pple::CardPools pairs_pools;
    pple::CardsBox pairs_box;
    // UI\Concentration\RANKS.XML, for ConRoundData::CalcEndGame (0x100080718).
    pple::ConcentrationRanks pairs_ranks;
    bool pairs_ranks_ok = false;
    // Kept so the click handler can reach the constants the peek timers use.
    pple::ConcentrationConstants pairs_constants;
    // The sneak-peek token rects, one per <NumStartTokens>, at /SneakPeekContainer.
    std::vector<SDL_FRect> pairs_tokens;
    std::vector<DrawItem> pairs_token_draws;
    pple::Movie pairs_sneak_movie;
    bool pairs_sneak_movie_ok = false;
    float pairs_sneak_frame = -1.0f;
    float pairs_sneak_x = -18.0f, pairs_sneak_y = -70.0f;
    std::vector<MeterDraw> pairs_meter;
    std::vector<JarCoin> pairs_jar_coins;
    float pairs_jar_origin_x = 0.0f, pairs_jar_origin_y = 0.0f;
    // The same `/imagepath` sprite unscaled, which is what an in-flight coin draws.
    SDL_Texture* pairs_coin_tex = nullptr;
    SDL_FRect pairs_coin_src{};
    float pairs_coin_w = 0.0f, pairs_coin_h = 0.0f, pairs_coin_reg_x = 0.0f, pairs_coin_reg_y = 0.0f;
    std::vector<ArcCoin> pairs_arcs;
    int pairs_arc_frames = 25;
    float pairs_arc_lid = -15.0f;
    float pairs_curjar_w = 112.0f, pairs_curjar_h = 112.0f;
    // Jar+0xC8, the number of coins on the tray.  Jar::DoFiller (0x100083B10) grows it from
    // `points / Board+0x28 * Round+0x12C`, and both of those are the CURRENT level's numbers
    // (Board::InitLevel 0x1000894E0 zeroes Round+0x12C and sets Board+0x28 to this level's card
    // count), so the fill restarts at each level rather than accumulating over the round.
    int pairs_jar_fill = 0;
    JarPhase pairs_jar_phase = JarPhase::Idle;
    std::map<std::string, BurstMovie> pairs_burst_cache;
    std::string pairs_burst_name;
    float pairs_burst_anchor_x = 0.0f, pairs_burst_anchor_y = 0.0f;
    float pairs_burst_frame = 0.0f;
    // The PlayArea node's placement, so a mouse position can be turned back into a cell.
    float pairs_origin_x = 0.0f, pairs_origin_y = 0.0f, pairs_outer_scale = 1.0f;
    std::string pairs_sheet_note;
    std::string pairs_back_xml = "UI\\Concentration\\blankcardbutton.xml";
    std::string pairs_outline = "UI\\Concentration\\highlite_outline.xml";
    // Card::InitUI's other nodes: Card+0xB0 is the level's "BlankCard" (blankcardbutton.xml),
    // which Card::OnHide shows in place of a matched card's art, and Card+0x110 holds four
    // SparkleSprite_y sparks parented to the Board's <PlayArea> node (Board+0x78), so their
    // coordinates are PlayArea-relative like the cards'.
    std::string pairs_blank_xml = "UI\\Concentration\\blankcardbutton.xml";
    std::string pairs_spark_xml = "UI\\Concentration\\SparkleSprite_y.xml";
    SDL_Texture* pairs_blank_tex = nullptr;
    SDL_FRect pairs_blank_src{};
    float pairs_blank_w = 0.0f, pairs_blank_h = 0.0f, pairs_blank_reg_x = 0.0f,
          pairs_blank_reg_y = 0.0f;
    SDL_Texture* pairs_spark_tex = nullptr;
    SDL_FRect pairs_spark_src{};
    float pairs_spark_w = 0.0f, pairs_spark_h = 0.0f, pairs_spark_reg_x = 0.0f,
          pairs_spark_reg_y = 0.0f;
    std::vector<CardCache> card_cache;
    // How many draws belong to the scene; everything past this is the card layer.
    std::size_t pairs_scene_draws = 0;
    std::vector<DrawItem> pairs_scene_layer;
    float pairs_scene_width = 800.0f, pairs_scene_height = 496.0f;
    std::vector<CardFlip> card_flip;
    std::vector<pple::CardFace> card_face_shown;
    std::vector<CardFx> card_fx;
    // Board::DoFlyIntoJar is called from Card::OnHide, i.e. when the flip-back ends, not when the
    // match is made, so the coin is spawned a few ticks after the pair turns over.
    std::vector<int> card_fx_pending_fly;
    TimeBarDraw timebar;
    ProfessorDraw professor;
    // Live HUD text.  Kept here so a tick can refresh it without rebuilding the board.
    std::string pairs_title = "Purble Pairs";
    std::size_t refresh_calls = 0;
    std::size_t build_calls = 0;
    Screen screen = Screen::Other;
    std::vector<Hotspot> menu_hotspots;
    std::vector<Hotspot> conc_hotspots;
    bool menu_ready = false;
    std::string menu_hover_name;
    // Set when build_pairs must rebuild the art for a board that already moved on: the Mixer
    // reshuffled the cards, or Board::NewLevel advanced to the next <Level>.
    bool pairs_keep_state = false;
    // One end-of-round sound per round.
    bool pairs_end_played = false;
    // Set by a click that changed the layout; the main loop does the rebuild.
    bool pairs_rebuild_pending = false;
    // ---- the main menu ---------------------------------------------------------------
    // PurblePlaceManager::Init (0x10007091C) walks UI\PurblePlace\PurblePlaceManager.xml by
    // @Name: <Background> (whose *text* is a sprite name, created at 0,0 -- LoadBackground
    // 0x100072DE4), <NodeSprite Name="Foreground">, two title labels, four <NodeButton>s and two
    // <Loading> labels that are then hidden.  Slot 0 of every button is transparent by design
    // (section 10.5): the icons are painted into PurblePlaceForeground.png and the buttons are
    // hot-spots over them, with slots 1 and 2 as the hover and pressed art.
    const std::string menu_xml = "UI\\PurblePlace\\PurblePlaceManager.xml";
    // PurblePlaceManager::Init (0x10007124B): a "Base" is created as a child of the Background
    // sprite, UI\PurblePlace\CloudsMovie.xml is built into it, and the container is put at
    // (-390,-300).  The movie is 1016 frames at 10 fps and the engine turns its loop off
    // (0x10007131E), so it plays once and stops on its last frame.
    const std::string menu_clouds_xml = "UI\\PurblePlace\\CloudsMovie.xml";
    pple::Movie menu_clouds;
    bool menu_clouds_ok = false;
    bool menu_clouds_loaded = false;
    float menu_cloud_fps = 10.0f;
    float menu_clouds_x = -390.0f, menu_clouds_y = -300.0f;
    // Where in scene_draws the clouds belong: right after the Background, so they sit behind the
    // Foreground's houses and show through the sky.
    std::size_t menu_cloud_at = 0;
    Uint64 menu_cloud_start = 0;
    // ---- tips, transitions and the in-canvas dialogs ---------------------------------
    // UI\Concentration\TIPS.XML, TipManager::parseXml (0x1000930B8), and the green style the
    // Concentration tips use (TipManager tipInfo+108 selects the style).
    std::vector<TipDef> tip_defs;
    bool tips_loaded = false;
    TipStyle tip_style;
    TipView tip;
    std::map<std::string, TTF_Font*> face_fonts;
    // The Professor's six movies, loaded once and kept across board rebuilds.
    std::map<std::string, pple::Movie> prof_movie_cache;
    // Everything drawn over the live game scene: the tip panel and the end-of-round panel.
    std::vector<DrawItem> overlay_draws;
    std::vector<DrawItem> result_draws;
    std::vector<UiButton> result_buttons;
    // The difficulty chooser is a whole screen of its own, so its draws go in scene_draws.
    std::vector<UiButton> picker_buttons;
    std::string picker_choice = "Beginner";
    // The chooser's own screen: the room background, the waiting Professor and the panel.
    std::vector<DrawItem> picker_draws;
    float picker_w = 800.0f, picker_h = 496.0f;
    float picker_time = 0.0f;
    int picker_frame = -1;
    Uint64 picker_last_tick = 0;
    // Jar::StopExplosion (0x100083A20) queues a between-boards tip and then calls
    // Board::NewLevel; the rebuild that follows has to show it again.
    std::string pending_level_tip;
    // `--intro` forces the Professor's enter animation and the intro tips, `--no-intro`
    // suppresses them, and an explicit `--pairs "D,L"` start skips them by default.  The board
    // itself is gated by the deal, not by the tips: Card::HandleEvent (0x10007C4A3) acts only
    // while Board+0x1E8 == 1, which Board::Update (0x10008A576) sets once no card is left in the
    // deal's state 6 -- and only then does it call DisplayIntro (0x10008A57D).
    bool intro_wanted = false;
    // DisplayIntro (0x10008C3F0) shows the two "How To Play" bubbles while ProIntro is enabled
    // and falls back to the single ProWelcome bubble for a game already seen.
    bool intro_seen = false;
    // The end of the round.  OnEnterWonGameState (0x1000821D0) and OnEnterLoseGameState
    // (0x10008259C) show the Professor's win/lose movie and a tip, and OnWinAnimStop
    // (0x100082670) / OnLoseAnimStop (0x10008272C) are where the shipped game opens its
    // score dialog (DialogBoxMCE template 221, or 236 under Media Center).
    bool result_shown = false;
    bool result_won = false;
    pple::ConcentrationBoard::EndGame last_end;
    bool last_end_ok = false;
};

} // namespace pple::viewer
