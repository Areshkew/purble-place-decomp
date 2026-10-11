#pragma once
#include "types.hpp"
#include "cakes_tools.hpp"
#include "cakes_transport.hpp"
#include "pple/cakes.hpp"

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
    // Which art the sheets are loaded from: the shipped 1x resources, or the game's own
    // double-DPI x2 resources (see the sheet-DPI block below and notes section 21).
    enum class SheetDpi { Base, Double };
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
    // The surface a sheet request turns into, at the current DPI: the 1x pair, the x2 sibling,
    // or the four x2 pages of a split sheet assembled into one canvas.  `src_scale` is what the
    // caller's source rectangles have to be multiplied by.
    SDL_Surface* load_sheet_surface(const std::string& requested, const std::string& resolved_name,
                                    float& src_scale, bool& composed, std::string& note);
    bool load_split_list();
    void clear_sheet_textures();
    void set_sheet_dpi(SheetDpi want);
    void rebuild_current_screen();
    float src_scale_for(SDL_Texture* tex) const;
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
    void spawn_arc_sparks(const ArcCoin& coin);
    bool advance_arc_sparks();
    void jar_grow();
    void arm_match_fx(const std::vector<int>& cells, bool immediate);
    void start_deal();
    bool advance_deal();
    void finish_deal();
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
    // What the time bar draws from: the first lit segment of <TimeBarGreen>'s 226, and whether the
    // clock has reached the <TimeBarRed> threshold.  The bar is part of the card layer's draw list,
    // so the tick loop has to notice a change in it even when no card moved.
    struct TimeBarState {
        int elapsed = 0;
        bool red = false;
    };
    TimeBarState time_bar_state() const;
    void load_menu_clouds();
    bool append_label(const pple::SceneItem& item);
    void build_loading();
    void build_menu();
    void load_current(bool announce);
    bool click_scene(float sx, float sy, bool motion_only);
    void update_hover(float sx, float sy);
    // ---- tips, the Professor's state machine and the in-canvas dialogs (tips.cpp,
    // professor.cpp, overlays.cpp) -------------------------------------------------------
    TTF_Font* face_font(const std::string& face, float size);
    std::string mui_text(int id);
    void measure_text(const std::string& face, float size, const std::string& text, float wrap,
                      int& w, int& h);
    void append_text(std::vector<DrawItem>& out, const std::string& face, float size,
                     std::uint32_t colour, const std::string& text, float x, float y, float wrap);
    // The bubble document: TIPS.XML for Purble Pairs, COMFYCAKESTIPS.XML for Comfy Cakes.
    void load_tips(const std::string& xml_name = "UI\\CONCENTRATION\\TIPS.XML");
    bool load_tip_style(TipStyle& style, const std::string& name);
    void load_tip_styles();
    const TipStyle& tip_style_for(const TipDef& def);
    const TipStyle& green_style();
    bool load_tip_slice(TipSlice& slice, const std::string& xml_name, int state);
    const TipDef* find_tip(const std::string& id) const;
    void show_tip(const std::string& id, float x, float y);
    void show_tip_def(const TipDef* def, const std::string& id, float x, float y);
    void show_simple_tip(const std::string& id, float x, float y);
    void update_simple_tip(const std::string& id, float x, float y);
    void update_hover_tip();    void append_panel(std::vector<DrawItem>& out, const TipStyle& style, float x, float y, float w,
                      float h);
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
    void draw_result_panel();
    // ---- the resumable savegame (pairs_state.cpp) -------------------------------------
    // ConcentrationGame::SaveGameState (0x100082B40) writes the round's XML through
    // XmlStash::SaveXMLFile into the same RGMH container as the settings, with a thumbnail of
    // the board ahead of it; LoadGameState reads it back and purges the menu's "saved game"
    // entry.  The viewer keeps that shape, with TIPS.XML's own element names for the round.
    std::string pairs_state_payload() const;
    bool write_pair_state(const std::string& path);
    bool load_pair_state(const std::string& path);
    std::vector<std::uint8_t> render_board_thumbnail(int width, int height) const;
    // The saved round itself, read once so the board and the resume screen's thumbnail come from
    // the same decode: `resume_state_valid` says the file held one.
    bool read_pair_state(const std::string& path);
    pple::ConcentrationBoard::RestoreState resume_state;
    bool resume_state_valid = false;
    std::vector<std::uint8_t> resume_thumbnail;
    std::string resume_summary;
    // The difficulty the saved round was played at, from its own XML; the board restores onto a
    // board dealt for it.
    std::string resume_difficulty;
    void build_difficulty();
    void draw_difficulty();
    void start_game(const std::string& difficulty, bool intro);
    void game_to_menu();
    // ---- Comfy Cakes -----------------------------------------------------------------
    // The second of the three games.  ComfyCakesGame::InitGameUI (0x10005D018) walks
    // COMFYCAKESGAME.XML by element name and creates every node itself, so the screen is a
    // scene-load plus the engine's own draw order and the few nodes it clones by count.
    void build_cakes();
    void draw_cakes();
    // Enter the room and start a round at a difficulty.
    void start_cakes(int difficulty);
    // Resolves one resolved scene item into a draw and, for a button, a hot-spot.  Shared with
    // build_cakes so the draw maths lives in one place.
    bool cakes_append(const pple::SceneItem& item, int state = 0, int frame = -1);
    void cakes_transport_load();
    bool cakes_transport_node(const pple::SceneItem& item, int state);
    void cakes_transport_draw(bool foreground);
    bool cakes_transport_advance(float dt);
    void cakes_transport_finish_slide();
    void cakes_transport_slide(int direction);
    void cakes_transport_position(int slot, float& x, float& y) const;
    void cakes_transport_ship(const pple::Cake& cake, bool correct, float x, float y);
    void cakes_next_target();
    CakeTransport cakes_transport;
    int cakes_minimum_moves = 0;
    // ---- Comfy Cakes: the tool machines -------------------------------------------
    // The six <Tool> machines of COMFYCAKESGAME.XML, their parts and the animations a button
    // click plays on them.  cakes_tools.cpp owns the parsing, the tick and the drawing.
    void cakes_tools_load();
    void cakes_tools_start(int station, int type, int shape, int slot);
    void cakes_tools_shake(int station);
    void cakes_tools_advance(float dt);
    void cakes_tools_stop();
    void cakes_tools_draw();
    // The belt slot a tool's station works on, from the tool's own position on the belt.
    int cakes_tool_slot_for(int station) const;
    bool cakes_tool_visible(int station) const;
    // Which of the six tool documents a station belongs to.
    const CakeTool* cakes_tool_for_station(int station) const;
    // The flavour index one machine part plays for a button's <Type>, and the direction the
    // engine runs it in (ToolIcing::PlayRotateAnimation reverses half its pairs).
    static std::size_t cakes_tool_flavour(const CakeToolPart& part, int type, int shape,
                                          bool& reverse);
    // Applies the layer a running machine animation was carrying, and runs
    // ComfyCakesGame::OnToolUsed's compare.
    void cakes_tool_complete();
    // ---- Comfy Cakes: the rules --------------------------------------------------------
    // The SDL-free model lives in pple::CakeModel (src/cakes.cpp); these methods only move its
    // state and log what happened.
    void cakes_load_model();
    void cakes_new_game(int difficulty);
    void cakes_use_tool(int station, int index);
    void cakes_slide_left();
    void cakes_slide_right();
    void cakes_judge();
    // Build the current target through the click path and slide it down the line.  --cakes-solve.
    void cakes_solve();
    void cakes_compute_score();
    void cakes_draw_cake(const pple::Cake& cake, float base_x, float base_y, int hide_top = 0,
                         float scale = 1.0f);
    // Where a cake's top layer sits inside its container, which is what the batter machine's
    // fill sprite is placed against.
    float cakes_cake_top_y(const pple::Cake& cake, int hide_top) const;
    // The belt slot that holds the cake being built, or -1 when the belt is empty.
    int cakes_working_slot() const;
    // Slide the belt until the cake being built sits under a given slot.
    void cakes_slide_to(int want);
    void cakes_refresh();
    // The per-frame half of cakes_refresh: the two animated layers are rewritten in place, so a
    // machine's animation and the Chef's movie play without rebuilding the room.
    void cakes_redraw();
    // The draw list with the cakes' layers included, for a capture run.
    void cakes_dump_draws();
    // ---- Comfy Cakes: the Purble Chef -----------------------------------------------
    void cakes_load_chef();
    void cakes_chef_show(int state, const std::string& tip_id);
    void cakes_chef_enter();
    const pple::Movie* cakes_chef_movie(const char* which) const;
    // Runs the Chef's state machine one step and reports whether his visible frame changed.
    bool cakes_chef_advance();
    void cakes_draw_chef();
    // The station (0..5) a tool button belongs to, from the tool document's <Station>.
    int cakes_tool_station(const std::string& button_name) const;
    // The six tool machines, by their COMFYCAKESGAME.XML <Tool Name=...> value ("BatterTool").
    // Each machine's container position, recorded by build_cakes from the same document.
    std::map<std::string, CakeTool> cakes_tools;
    std::map<std::string, std::pair<float, float>> cakes_tool_anchor;
    bool cakes_tools_tried = false;
    // The machine animation in flight.  Tool::UseTool (0x1000751CC) starts it and
    // Tool::FinishAddTool (0x10007548A) applies the layer when it completes; while it runs the
    // cake's top layer is hidden (PlayAddToolAnimation 0x100075F46) and the machine's own
    // falling stream and fill sprite carry the ingredient in.
    bool cakes_tool_busy = false;
    int cakes_tool_station_running = -1;
    int cakes_tool_slot_running = -1;
    int cakes_tool_type_running = -1;
    int cakes_tool_shape_running = 0;
    float cakes_tool_time = 0.0f;
    float cakes_tool_total = 0.0f;
    // A refusal's shake, on the machine Base; -1 when nothing is shaking.
    float cakes_tool_shake = -1.0f;
    int cakes_tool_shake_station = -1;
    // One machine part of the running animation, with the flavour index it plays.
    std::vector<CakeToolPlaying> cakes_tool_playing;
    pple::Cake cakes_tool_before; // immutable pre-use stack while the new ingredient is animated
    int cakes_machine_type[6] = {};
    float cakes_tool_reveal_at = 0;
    bool cakes_tool_replacing = false;
    // Where the static room ends in scene_draws, so the per-frame half of a refresh can rewrite
    // everything after it (the Chef, the cakes and the machines) without rebuilding the room.
    std::size_t cakes_room_at = 0;

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
    bool cakes_mode = false;
    // 0 Beginner, 1 Intermediate, 2 Advanced, for --cakes' optional difficulty.
    int cakes_difficulty_choice = 0;
    // --cakes-solve plays a whole round out through the click path (the verification hook).
    bool cakes_solve_hook = false;
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
    // --pairs-state PATH saves the round at the end of a run and resumes it when the file
    // already holds one, which is the viewer's stand-in for the shipped game's own
    // ConcentrationStateSave.xml.
    std::string pairs_state_path;
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
    // ---- double-DPI art: the engine's g_bDoubleDPI path --------------------------------
    // ResourceTextureD3D::SetNameForDPI (0x1000A80DC) inserts "x2" before the extension, so
    // every image resource has a twice-size sibling (UI\PurblePlace\Loading.jpg ->
    // Loadingx2.jpg, 800x496 -> 1600x992).  ResourceTextureD3D::GetSplitTexture (0x1000A7E40)
    // builds `<stem>x2<page>.sht` for a sheet SplitList.xml lists, i.e. the sheet's four
    // quadrants as 1024x1024 pages, and Material::Render (0x1000A96E0) picks the page by UV
    // quadrant.  The x2 texture is exactly twice the 1x padded texture, so a source rectangle
    // authored in 1x space is read at twice its offset and size; the layout, the registration
    // and every destination rectangle are unchanged.  See notes section 21.
    // enum class SheetDpi { Base, Double };  -- declared at the top of the class
    SheetDpi sheet_dpi = SheetDpi::Base;
    // 0 auto, 1 forced by --hidpi, -1 forced by --no-hidpi.  Auto uses the x2 art above
    // `hidpi_scale`, where upscaling the 1x art would visibly soften the screen.
    int hidpi_mode = 0;
    float hidpi_scale = 1.5f;
    std::vector<pple::SplitSheet> split_sheets;
    bool split_sheets_loaded = false;
    // Source-rect multiplier per texture/sheet: 2 for a sheet drawn from its x2 resources.
    std::map<SDL_Texture*, float> texture_src_scale;
    std::map<std::string, float> sheet_src_scale;
    std::size_t hidpi_sheets = 0, hidpi_fallbacks = 0;
    bool dpi_logged = false;
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
    // The two sparkle sprites the flying coins emit, and the live particles.
    SDL_Texture* pairs_spark_gold_tex = nullptr;
    SDL_FRect pairs_spark_gold_src{};
    SDL_Texture* pairs_spark_white_tex = nullptr;
    SDL_FRect pairs_spark_white_src{};
    std::vector<ArcSpark> pairs_arc_sparks;
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
    // Card+0xA8: Card::InitUI (0x10007BDFA) builds the node it names "SneakIcon" from
    // SneakPeakMiniIcon.xml as a child of the card's own Base, so it inherits the card's scale,
    // and Card::DoSneakIcon (0x10007DE60) shows it while Card+0x18 -- the flag
    // Board::SelectSneakCard (0x10008B348) sets on one random card -- is set and Card+0x568 is
    // below 2, i.e. while the card is still in play and face down or up.
    std::string pairs_sneak_icon_xml = "UI\\Concentration\\SneakPeakMiniIcon.xml";
    SDL_Texture* pairs_sneak_icon_tex = nullptr;
    SDL_FRect pairs_sneak_icon_src{};
    float pairs_sneak_icon_w = 0.0f, pairs_sneak_icon_h = 0.0f;
    float pairs_sneak_icon_reg_x = 0.0f, pairs_sneak_icon_reg_y = 0.0f;
    // Card::InitUI's remaining nodes, all children of the card's own Base:
    //   Card+0xC0  Card_shadow.xml        placed at (2,-2) (0x10007BB09)
    //   Card+0xE8  tileglow.xml           placed at (-18,-14), or (-16,-10) when the cell is
    //                                     wider than one (0x10007BF97)
    //   Card+0xF0  tilecover.xml          placed at (-1, 4) (0x10007C009)
    // and the BonusTileAnimation movie (Card+0xE0), built from the scene document's
    // /BonusTileAnimation by ConUILoader::LoadMovie with its Base at
    // (card.w/2 - 500, card.h/2 - 400) (0x10007BED2).  Only Card::PlayBonusAnim
    // (0x10007FA7C) touches the movie and the glow, and the tile cover belongs to
    // Card::Update's counter at 0x10007DC9A.
    std::string pairs_shadow_xml = "UI\\Concentration\\Card_shadow.xml";
    std::string pairs_tileglow_xml = "UI\\Concentration\\tileglow.xml";
    std::string pairs_tilecover_xml = "UI\\Concentration\\tilecover.xml";
    SDL_Texture* pairs_shadow_tex = nullptr;
    SDL_FRect pairs_shadow_src{};
    float pairs_shadow_w = 0.0f, pairs_shadow_h = 0.0f;
    SDL_Texture* pairs_tileglow_tex = nullptr;
    SDL_FRect pairs_tileglow_src{};
    float pairs_tileglow_w = 0.0f, pairs_tileglow_h = 0.0f;
    SDL_Texture* pairs_tilecover_tex = nullptr;
    SDL_FRect pairs_tilecover_src{};
    float pairs_tilecover_w = 0.0f, pairs_tilecover_h = 0.0f;
    pple::Movie pairs_bonus_movie;
    bool pairs_bonus_movie_ok = false;
    std::vector<CardCache> card_cache;
    // How many draws belong to the scene; everything past this is the card layer.
    std::size_t pairs_scene_draws = 0;
    std::vector<DrawItem> pairs_scene_layer;
    float pairs_scene_width = 800.0f, pairs_scene_height = 496.0f;
    std::vector<CardFlip> card_flip;
    std::vector<pple::CardFace> card_face_shown;
    std::vector<CardFx> card_fx;
    // The deal-in, per card: Card+0x40 is `rand() % 60` ticks of delay (Board::InitLevel
    // 0x1000895C7) and Card+0x14 counts Card::DoIntroMove's 45 ticks.  Card+0x14 = 45 means the
    // card has arrived; Board+0x1E8 (`pairs_dealing`) stays 0 until every card has.
    std::vector<int> card_deal_delay;
    std::vector<int> card_deal_tick;
    bool pairs_dealing = false;
    bool deal_enabled = true;
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
    // Comfy Cakes: the screen's own buttons.  ReturnToMainMenu, the two belt-arrow buttons and
    // each tool's flavour buttons all take their focus from the pointer, exactly as
    // UserInterface::ProcessMouseMove -> SetFocus does on every screen.
    std::vector<Hotspot> cakes_hotspots;
    // The engine's draw order, one name per scene_draws entry, so the list can be read back.
    std::vector<std::string> cakes_draw_names;
    // A tool button's <Type> (a LAYERS.xml entry) by its element name, so a click can name the
    // layer it applies: "CakePanRoundButton" -> 3, "BatterChocolateButton" -> 0.
    std::map<std::string, int> cakes_button_type;
    // UI\COMFYCAKES\COMFYCAKESGAME.XML, the Comfy Cakes room document.
    const std::string cakes_xml = "UI\\COMFYCAKES\\COMFYCAKESGAME.XML";
    bool cakes_ready = false;
    // The hot-spot the pointer is over on the Comfy Cakes screen.  The engine's focus is one
    // node (UserInterface::ProcessMouseMove 0x100098498 -> SetFocus 0x1000992E0), and this room
    // rebuilds on a change exactly as the menu does.
    std::string cakes_hover_name;
    // The Comfy Cakes play state.  The model's data (LAYERS.XML, COMFYCAKESVARS.XML) is loaded
    // once; the round's own state is rebuilt by cakes_new_game.
    pple::CakeModel cakes_model;
    bool cakes_model_tried = false;
    int cakes_difficulty = 0;
    // The belt, one entry per ConveyorBelt slot (nine), plus whether that slot holds a cake.
    std::vector<pple::Cake> cakes_belt;
    std::vector<bool> cakes_belt_present;
    // The slot the tools work on: position 3 on Beginner, 2 otherwise.
    int cakes_current_slot = 3;
    // Comfy Cakes' own clock: how long since the last frame, for the tool machines' animations.
    Uint32 cakes_tick_at = 0;
    float cakes_tick = 0.0f;
    pple::Cake cakes_target;
    int cakes_correct = 0, cakes_missed = 0, cakes_moves = 0, cakes_slides = 0;
    int cakes_score = 0, cakes_bonus = 0, cakes_final = 0;
    bool cakes_over = false, cakes_won = false, cakes_matched_seen = false;
    // The Purble Chef: 0 hidden, 1 idle, 2 entering, 3 leaving, 4 talking, 5 win, 6 lose.
    int cakes_chef_state = 0;
    int cakes_chef_pending = -1;
    std::string cakes_chef_pending_tip;
    float cakes_chef_frame = -1.0f;
    float cakes_chef_fps = 12.0f;
    bool cakes_chef_loaded = false;
    std::map<std::string, pple::Movie> cakes_chef_movies;
    bool menu_ready = false;
    std::string menu_hover_name;
    // Hover.  The engine's focus follows the mouse: UserInterface::ProcessMouseMove (0x100098498)
    // hit-tests the node under the cursor and calls SetFocus (0x1000992E0), which sends
    // Event_FocusLost / Event_FocusGot (0x12 / 0x11, built by Event_FocusLost::Create
    // 0x100094ADC and Event_FocusGot::Create 0x100094AA8) to the old and the new node.  Exactly
    // one node is focused at a time, which is why these three are mutually exclusive:
    //   * a card shows highlite_outline.xml (Card+0xD0) on FocusGot, and card_glow.xml
    //     (Card+0xC8) only while it is face up -- Card::HandleEvent's case 0x11
    //     (0x10007C46F) and Card::OnReveal (0x10007F849);
    //   * a NodeButton switches to its second <Animation> -- NodeButton::HandleEvent
    //     0x1000AAC92 (state 1 on <enter>, 0x1000AAE1C state 2 on <down>).
    int hover_card = -1;      // index into card_cache, -1 = no card under the pointer
    int hover_token = -1;     // index into pairs_token_draws
    int hover_button_at = -1; // index into conc_hotspots
    // card_glow.xml, the halo a hovered face-up card draws over itself.
    std::string pairs_glow_xml = "UI\\Concentration\\card_glow.xml";
    SDL_Texture* pairs_glow_tex = nullptr;
    SDL_FRect pairs_glow_src{};
    float pairs_glow_w = 0.0f, pairs_glow_h = 0.0f;
    // The same tokens in their hover state, index-aligned with pairs_token_draws.
    std::vector<DrawItem> pairs_token_hover_draws;
    // Where each <NodeButton> of the current scene landed in scene_draws, so a hovered one can
    // be swapped for its second <Animation> in place instead of being redrawn on top.
    std::map<std::string, int> scene_button_at;
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
    // UI\Concentration\TIPS.XML, TipManager::parseXml (0x1000930B8), and the three style
    // documents TipManager keeps -- one per <Type>; ShowTipIfEnabledAtLocV indexes them with
    // `tipInfo + 108` (`this + 3 * type + 8`, 0x100092C06).
    std::vector<TipDef> tip_defs;
    bool tips_loaded = false;
    // Which bubble documents have been parsed, so the Comfy Cakes one does not re-read TIPS.XML.
    std::set<std::string> tips_loaded_documents;
    std::map<std::string, TipStyle> tip_styles;
    bool tip_styles_loaded = false;
    TipView tip;
    // The simple, object-owned tips (CONTipManager::ShowSimpleTip 0x100085C8C) follow the hover,
    // and their state has to be re-derived rather than driven by the mouse-motion event: a card
    // that becomes matched while the pointer is already over it must still raise `BlankCard`.
    // `hover_simple_shown` is the id that is up (or empty), and `hover_simple_expired` is the id
    // whose <Duration> has just run out while the pointer stayed put, so it is not re-shown until
    // the pointer leaves that object.
    std::string hover_simple_shown;
    std::string hover_simple_expired;
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
