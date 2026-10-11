#include "app.hpp"

namespace pple::viewer {

// Comfy Cakes is the second of the three games.  Its room is not a "layout" document the way
// CONCENTRATIONGAME.XML is: ComfyCakesGame::InitGameUI (0x10005D018) looks each element up by
// name and creates the node itself, so the drawing order is the creation order in that function
// and a handful of nodes are cloned by count rather than by the document.
//
// Engine order, from InitGameUI:
//   1  /Background                    PurblePlaceUILoader::LoadBackground
//   2  /RightBelt, then its anim      LoadGraphic
//   3  /ConveyorPlatform, then anim
//   4  /ConveyorBelt                   ComfyCakesUILoader::LoadConveyorBelt (0x100061964):
//        LeftBeltSprite, 7 x MainBeltSprite (each 99 px further right, the seventh switched to
//        the belt's second <Animation>), the "left belt cake" Base, BackWall, then the
//        "main belt cake" Base
//   5  /CakeScreen                     ComfyCakesUILoader::LoadCakeScreen (0x1000620B8):
//        BackgroundSprite, ScreenSprite (0,20), GlassSprite, HighlightSprite
//   6..9  /SlideLeftButton, /SlideLeftHighlightSprite, /SlideRightButton, /SlideRightHighlightSprite
//  10  /RedLight, its anim
//  11  /CakeBox, its anim
//  12  /TrashCan, its anim
//  13  /ReturnToMainMenu
//  14  /Tools, one per <Tool>, each ComfyCakesUILoader::LoadTool -> Tool::Init:
//        the button-area Base, the machine Base, /HighlightSprite, then every <ButtonTool> of
//        the tool's own document, and then that tool subclass's own sprites
//  15  TimerBackSprite, TimerBarSprite  built by name (XmlManager::GetXml 0x10005DB51 /
//        0x10005DBAE), parented to the game Base and positioned (-10,325) and (12,346)
//  16  /ShippingSignBackground, /ShippingSign, their anims
//  17  /ScreenSlot eight times (0x10005DE17): four across at 20 px, then a second row 30 px down
//  18  /DroppingBox, its anim
//  19  /PurbleChef                     ComfyCakesUILoader::LoadPurbleChef (0x1000621B4)
//  20  the Media Center header buttons and labels, only when g_bMediaCenter == 1 (0x10005E0DB)
//  21  the tip container, moved to the front at the end of InitGameUI (0x10005E313)
namespace {

// Where an item sits in that list.  A widget child is keyed by its parent element and its own
// element name; a top-level instance by its instance or Name.  Anything the table does not name
// keeps its document order, which is how the tools' own sprites and the MCE nodes behave.
std::string cakes_item_key(const pple::SceneItem& item) {
    const std::string& key = item.name.empty() ? item.instance : item.name;
    // Bare properties are already qualified. Qualifying the TV frame twice gives
    // it the wrong rank and paints its black screen over the purple screen sprite.
    return item.widget_path.empty() || key.rfind(item.widget_path, 0) == 0
               ? key : item.widget_path + key;
}

int cakes_rank(const pple::SceneItem& item) {
    const std::string full = cakes_item_key(item);
    static const struct {
        const char* key;
        int rank;
    } kOrder[] = {
        {"Root/Background", 10},
        {"RightBelt", 20},
        {"ConveyorPlatform", 30},
        {"ConveyorBelt/LeftBeltSprite", 40},
        {"ConveyorBelt/MainBeltSprite", 50},
        {"ConveyorBelt/BackWall", 60},
        {"CakeScreen/BackgroundSprite", 70},
        {"CakeScreen/ScreenSprite", 71},
        {"CakeScreen/GlassSprite", 72},
        {"CakeScreen/HighlightSprite", 73},
        {"SlideLeftButton", 80},
        {"SlideLeftHighlightSprite", 81},
        {"SlideRightButton", 82},
        {"SlideRightHighlightSprite", 83},
        {"RedLight", 90},
        {"CakeBox", 100},
        {"TrashCan", 110},
        {"ReturnToMainMenu", 120},
        {"ShippingSignBackground", 140},
        {"ShippingSign", 141},
        {"ScreenSlot", 150},
        {"DroppingBox", 160},
        {"PurbleChef", 170},
    };
    for (const auto& entry : kOrder)
        if (full == entry.key) return entry.rank;
    // Inside a tool: Tool::Init builds HighlightSprite, then the buttons; the subclass adds the
    // rest afterwards, which is the order the tool document lists them in.
    if (item.instance == "HighlightSprite") return 200;
    if (item.instance == "ButtonTool") return 210;
    return 190;
}

// One draw together with where the engine put it, so the engine's own clones can be sorted into
// the same list instead of being appended at the end.
struct CakesEntry {
    pple::SceneItem item;
    int rank = 0;
    // Which <Animation> to draw.  0 is what an untouched node draws; 1 is the belt's end piece
    // and any button's <enter> state.
    int state = 0;
};

}  // namespace

// One resolved item: its sheet frame, its destination rect (NodeSprite::UpdateZero 0x100093740
// subtracts the scaled registration) and, for a button, the hot-spot over it.  `state` picks
// which <Animation> a button draws: the engine starts at 0 and moves to 1 on <enter>
// (NodeButton::HandleEvent 0x1000AAC92), whose last frame is the focus art.
bool App::cakes_append(const pple::SceneItem& item, int state, int frame) {
    if (item.kind != pple::NodeKind::Sprite && item.kind != pple::NodeKind::Button) return false;
    if (item.ref.empty()) return false;
    const std::string& key = item.name.empty() ? item.instance : item.name;
    // Exactly one node is focused (UserInterface::ProcessMouseMove -> SetFocus), and a button's
    // focus art is the last frame of its second <Animation> (NodeButton::HandleEvent
    // 0x1000AAC92).  The screen is rebuilt on a hover change, as the menu is.
    int draw_state = state;
    if (item.kind == pple::NodeKind::Button && key == cakes_hover_name) draw_state = 1;
    const Sprite sprite = resolve_sprite(bundle, item.ref, draw_state);
    if (!sprite.ok) return false;
    std::string sheet_how;
    const auto* sheet_slot = bundle.resolve(sprite.requested, sheet_how);
    if (sheet_slot == nullptr) return false;
    std::string note;
    SDL_Texture* tex = load_sheet_texture(sprite.requested, sheet_slot->entry->name, note);
    if (tex == nullptr) return false;
    pple::SceneItem geometry;
    geometry.src_x = sprite.src.x;
    geometry.src_y = sprite.src.y;
    geometry.src_w = sprite.src.w;
    geometry.src_h = sprite.src.h;
    geometry.frames = sprite.frames;
    geometry.per_row = sprite.per_row;
    geometry.prepped = sprite.prepped;
    float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
    pple::sprite_frame_rect(geometry, frame >= 0 ? frame : (draw_state == 0 ? 0 : sprite.frames - 1), fx, fy, fw, fh);
    DrawItem d;
    d.texture = tex;
    d.src = SDL_FRect{fx, fy, fw, fh};
    d.dst = SDL_FRect{item.x - sprite.reg_x * item.scale, item.y - sprite.reg_y * item.scale,
                      fw * item.scale, fh * item.scale};
    d.alpha = item.alpha;
    scene_draws.push_back(d);
    cakes_draw_names.push_back(item.name.empty() ? item.instance : item.name);
    scene_w = std::max(scene_w, d.dst.x + d.dst.w);
    scene_h = std::max(scene_h, d.dst.y + d.dst.h);
    if (item.kind == pple::NodeKind::Button) {
        Hotspot spot;
        spot.name = key;
        spot.rect = d.dst;
        spot.idle_at = static_cast<int>(scene_draws.size()) - 1;
        spot.has_hover = sprite.frames > 1;
        cakes_hotspots.push_back(spot);
        scene_button_at[key] = spot.idle_at;
    }
    return true;
}

// The whole room, in the engine's order, plus the nodes InitGameUI builds itself.
void App::build_cakes() {
    release_draws();
    scene_button_at.clear();
    cakes_hotspots.clear();
    cakes_draw_names.clear();
    scene_w = scene_h = 0.0f;
    cakes_ready = false;
    std::string how;
    const auto* slot = bundle.resolve(cakes_xml, how);
    if (slot == nullptr) {
        std::fprintf(stderr, "cakes: %s not found\n", cakes_xml.c_str());
        return;
    }
    const auto fetch = [&](const std::string& name, std::vector<std::uint8_t>& out) {
        std::string inner_how;
        const auto* s = bundle.resolve(name, inner_how);
        if (s == nullptr) return false;
        out = bundle.read(*s);
        return true;
    };
    pple::Scene scene;
    std::string error;
    if (!pple::scene_load(bundle.read(*slot), fetch, scene, error)) {
        std::fprintf(stderr, "cakes parse: %s\n", error.c_str());
        return;
    }
    if (container_offsets) pple::apply_container_offsets(scene);
    loaded_scene = scene;

    // Everything that will be drawn, with the rank the engine gave it.  The document's own items
    // carry their document index as the tie-break, so a stable sort keeps them in that order
    // inside a rank group -- which is exactly what the engine's walk does.
    std::vector<CakesEntry> entries;
    entries.reserve(scene.items.size() + 16);
    for (std::size_t i = 0; i < scene.items.size(); ++i) {
        CakesEntry entry;
        entry.item = scene.items[i];
        entry.rank = cakes_rank(scene.items[i]);
        entries.push_back(std::move(entry));
    }

    // ConveyorBelt::Init builds the main belt as seven copies of /MainBeltSprite, each 99 px
    // further right than the one before it, and switches the seventh to the belt's second
    // <Animation> (NodeSprite::SetAnimation(node, 1, true, 1) at 0x100062543).  The document
    // carries the first copy only; the copies take the ranks 50 with a document index after it.
    const pple::SceneItem* main_belt = nullptr;
    for (std::size_t i = 0; i < scene.items.size(); ++i) {
        if (scene.items[i].instance == "MainBeltSprite") {
            main_belt = &scene.items[i];
            break;
        }
    }
    int belt_tiles = 0;
    if (main_belt != nullptr) {
        for (int i = 1; i < 7; ++i) {
            CakesEntry entry;
            entry.item = *main_belt;
            entry.item.x = main_belt->x + 99.0f * static_cast<float>(i);
            entry.item.instance = "MainBeltSprite";
            entry.rank = 50;
            // Tile 6 is the belt's end: ConveyorBelt::Init switches it to animation 1.
            entry.state = i == 6 ? 1 : 0;
            entries.push_back(std::move(entry));
            ++belt_tiles;
        }
    }

    // InitGameUI clones /ScreenSlot eight times: four across at 20 px, then a second row 30 px
    // down.  They are the queue readout beside the shipping sign.
    const pple::SceneItem* screen_slot = nullptr;
    for (const pple::SceneItem& item : scene.items) {
        if (item.instance == "ScreenSlot") {
            screen_slot = &item;
            break;
        }
    }
    int slot_copies = 0;
    if (screen_slot != nullptr) {
        for (int i = 1; i < 8; ++i) {
            CakesEntry entry;
            entry.item = *screen_slot;
            entry.item.x = screen_slot->x + 20.0f * static_cast<float>(i % 4);
            entry.item.y = screen_slot->y + (i >= 4 ? 30.0f : 0.0f);
            entry.rank = 150;
            entries.push_back(std::move(entry));
            ++slot_copies;
        }
    }

    // The timer's two sprites are built by name, not by the document.
    int timer_sprites = 0;
    if (cakes_difficulty != 0) { // NewGame -> ShowTimer(false) on Beginner.
        struct TimerSprite {
            const char* name;
            const char* xml;
            float x, y;
            int rank;
        };
        static const TimerSprite kTimer[] = {
            {"TimerBackSprite", "UI\\ComfyCakes\\TimerBackSprite.xml", -10.0f, 325.0f, 130},
            {"TimerBarSprite", "UI\\ComfyCakes\\TimerBarSprite.xml", 12.0f, 346.0f, 131},
        };
        for (const TimerSprite& timer : kTimer) {
            CakesEntry entry;
            entry.item.kind = pple::NodeKind::Sprite;
            entry.item.instance = timer.name;
            entry.item.ref = timer.xml;
            entry.item.x = timer.x;
            entry.item.y = timer.y;
            entry.item.scale = 1.0f;
            entry.item.alpha = 255;
            entry.rank = timer.rank;
            entries.push_back(std::move(entry));
            ++timer_sprites;
        }
    }

    std::stable_sort(entries.begin(), entries.end(),
                     [](const CakesEntry& a, const CakesEntry& b) { return a.rank < b.rank; });

    // The six tools, in x order, each with the prefix its buttons share.  A <Tool> element's own
    // Name is "CakePanTool" and so on; its buttons are the CakePan*/Batter*/... ones, the same
    // prefixes cakes_tool_station uses.  The pairings let a tool's highlight know which hovered
    // button belongs to it.
    static const struct {
        const char* tool;
        const char* prefix;
    } kToolPrefix[] = {
        {"CakePanTool", "CakePan"},       {"BatterTool", "Batter"},
        {"FillingTool", "Filling"},       {"IcingTool", "Icing"},
        {"DecorationTool", "Decoration"}, {"FinalTouchTool", "Final"},
    };
    std::vector<std::pair<float, std::string>> tools;
    for (const pple::SceneItem& container : scene.containers) {
        if (!container.widget || container.instance != "Tool") continue;
        for (const auto& entry : kToolPrefix)
            if (container.name == entry.tool) {
                tools.push_back({container.x, entry.prefix});
                break;
            }
    }
    std::sort(tools.begin(), tools.end());
    // The machines' own containers.  A <Tool> element's X/Y is where the engine puts its
    // button-area Base, and every machine part of that tool's document is positioned inside it,
    // which is what cakes_tools.cpp needs to draw a machine's animation.
    cakes_tool_anchor.clear();
    for (const std::pair<float, std::string>& tool : tools)
        cakes_tool_anchor[tool.second + "Tool"] = {tool.first, 298.0f};
    for (const pple::SceneItem& container : scene.containers) {
        if (!container.widget || container.instance != "Tool") continue;
        const auto it = cakes_tool_anchor.find(container.name);
        if (it != cakes_tool_anchor.end()) {
            const int station = cakes_tool_station(container.name);
            const float shift = cakes_difficulty == 0 && (station == 0 || station == 1)
                                    ? 100.0f : 0.0f;
            // ShowTools always offsets the pan's machine Base by 25, not its buttons.
            it->second = {container.x + shift + (station == 1 ? 25.0f : 0.0f), container.y};
        }
    }

    // The engine creates every /HighlightSprite hidden and shows it while the button it belongs
    // to has the focus: ComfyCakesGame::InitGameUI clears the slide highlights' visible flag at
    // node+64 (0x10005D449 / 0x10005D5D5), Tool::Init the tool one (0x100074EDD) and
    // CakeScreen::Init the TV one (0x100057A9C), each also stopping its HighlightAnim's 0.25 s
    // alpha fade-in.  So a highlight is drawn only for the hovered control, and the TV's never
    // at rest -- the game raises it on its own events, which this reconstruction does not model.
    // The six tool highlights are the rank-200 group; in x order they pair with the tools.
    std::vector<std::size_t> tool_highlight_entries;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const pple::SceneItem& item = entries[i].item;
        const std::string full = cakes_item_key(item);
        if (entries[i].rank == 200 && full == "Tool/HighlightSprite")
            tool_highlight_entries.push_back(i);
    }
    std::sort(tool_highlight_entries.begin(), tool_highlight_entries.end(),
              [&entries](std::size_t a, std::size_t b) {
                  return entries[a].item.x < entries[b].item.x;
              });
    std::map<std::size_t, std::string> highlight_tool;
    for (std::size_t i = 0; i < tool_highlight_entries.size() && i < tools.size(); ++i)
        highlight_tool[tool_highlight_entries[i]] = tools[i].second;
    std::string hover_tool;
    for (const auto& tool : tools)
        if (cakes_hover_name.rfind(tool.second, 0) == 0) hover_tool = tool.second;

    // InitGameUI builds the Media Center header nodes only when g_bMediaCenter == 1 (0x10005E0DB),
    // and this build's executable deletes the matching five nodes of its own game when it is 0
    // (0x10008122B), so they are absent by default here too; --mce keeps them.
    std::size_t sprites = 0, unresolved = 0;
    cakes_button_type.clear();
    cakes_transport_load();
    cakes_transport.nodes.clear();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const CakesEntry& entry = entries[i];
        pple::SceneItem item = entry.item;
        if (item.widget_path == "Tool/") {
            const int station = cakes_tool_station(item.widget_name);
            if (!cakes_tool_visible(station)) continue;
            // All machine parts are owned by the dynamic layer, including their idle state.
            const CakeTool* tool = cakes_tool_for_station(station);
            if (tool != nullptr && std::any_of(tool->parts.begin(), tool->parts.end(),
                    [&item](const CakeToolPart& part) { return part.sprite == item.ref; }))
                continue;
            if (cakes_difficulty == 0 && (station == 0 || station == 1)) item.x += 100.0f;
        }
        const std::string& key = item.name.empty() ? item.instance : item.name;
        const std::string full = cakes_item_key(item);
        // This document spells the two Media Center header nodes NewGameButton / ExitGameButton
        // (ConcentrationGame's own document spells them NewGame / ExitGame).
        const bool mce_node = key == "NewGameButton" || key == "ExitGameButton" ||
                              key == "NewGameLabel" || key == "ExitGameLabel";
        if (mce_node && !mce_mode) continue;
        // The two MCE labels are the only labels the document carries; everything else in a
        // Comfy Cakes room is a sprite or a button.
        if (item.kind == pple::NodeKind::Label) continue;
        if (item.kind == pple::NodeKind::Movie) continue;
        // A highlight is only drawn for the control it belongs to while that control is focused.
        const std::map<std::size_t, std::string>::const_iterator highlight = highlight_tool.find(i);
        if (highlight != highlight_tool.end()) {
            if (hover_tool.empty() || highlight->second != hover_tool) continue;
        } else if (full == "SlideLeftHighlightSprite" || full == "SlideRightHighlightSprite") {
            const bool left = full == "SlideLeftHighlightSprite";
            const std::string button_name = left ? "SlideLeftButton" : "SlideRightButton";
            if (cakes_hover_name != button_name) continue;
            // Centre the halo on the button's draw rect. The authored halo's top is
            // 9 px above that centre; using it unchanged leaves a displaced side glow.
            const auto button = std::find_if(entries.begin(), entries.end(), [&](const CakesEntry& e) {
                return e.item.instance == button_name;
            });
            if (button != entries.end()) {
                const Sprite face = resolve_sprite(bundle, button->item.ref, 0);
                const Sprite halo = resolve_sprite(bundle, item.ref, 0);
                if (face.ok && halo.ok) {
                    item.x = button->item.x - face.reg_x + (face.src.w - halo.src.w) / 2 + halo.reg_x;
                    item.y = button->item.y - face.reg_y + (face.src.h - halo.src.h) / 2 + halo.reg_y;
                }
            }
        } else if (full == "CakeScreen/HighlightSprite") {
            continue;
        }
        // A tool button names the LAYERS.xml entry it applies in its <Desc><Type>, which the
        // core loader reads for every <ButtonTool>.
        if (item.kind == pple::NodeKind::Button && item.tool_type >= 0)
            cakes_button_type[key] = item.tool_type;
        if (cakes_transport_node(item, entry.state)) continue;
        if (cakes_append(item, entry.state)) ++sprites;
        else ++unresolved;
    }

    // The room's design bounds are the background photograph's 800x496, exactly as the Pairs
    // board keeps its own (pairs_render.cpp).  A sprite that runs past them is an offscreen
    // part -- the DroppingBox's 150x270 frame reaches x = 825 and the Media Center header art
    // starts at negative y -- so it is clipped by the viewport rather than shrinking the scene.
    const float raw_w = scene_w, raw_h = scene_h;
    scene_w = pple::kScreenWidth;
    scene_h = pple::kScreenHeight;
    cakes_ready = true;
    std::printf("cakes %s: %zu sprite(s) drawn, %d extra belt tile(s), %d extra screen slot(s), "
                "%d timer sprite(s), %zu hot-spot(s), %zu unresolved\n",
                cakes_xml.c_str(), sprites, belt_tiles, slot_copies, timer_sprites,
                cakes_hotspots.size(), unresolved);
    std::printf("cakes extent %.0fx%.0f of a %.0fx%.0f viewport (%.0f px right, %.0f px below)\n",
                static_cast<double>(raw_w), static_cast<double>(raw_h),
                static_cast<double>(pple::kScreenWidth), static_cast<double>(pple::kScreenHeight),
                static_cast<double>(raw_w - pple::kScreenWidth),
                static_cast<double>(raw_h - pple::kScreenHeight));
    for (const std::string& problem : scene.problems)
        std::printf("  problem: %s\n", problem.c_str());
    for (const Hotspot& spot : cakes_hotspots)
        std::printf("  hot-spot %-26s %.0f,%.0f %.0fx%.0f\n", spot.name.c_str(),
                    static_cast<double>(spot.rect.x), static_cast<double>(spot.rect.y),
                    static_cast<double>(spot.rect.w), static_cast<double>(spot.rect.h));
    hud_lines = {cakes_xml, std::to_string(sprites) + " sprites, " +
                                 std::to_string(cakes_hotspots.size()) + " hot-spots"};
    rebuild_hud();
}

// One line per draw with the layer rects included, so a capture can be checked against the
// background photograph block by block (cakes_compare.py).
void App::cakes_dump_draws() {
    if (!offscreen) return;
    for (std::size_t i = 0; i < scene_draws.size() && i < cakes_draw_names.size(); ++i) {
        const DrawItem& d = scene_draws[i];
        std::printf("  draw %-28s %.0f,%.0f %.0fx%.0f\n", cakes_draw_names[i].c_str(),
                    static_cast<double>(d.dst.x), static_cast<double>(d.dst.y),
                    static_cast<double>(d.dst.w), static_cast<double>(d.dst.h));
        std::printf("  source %-26s %.0f,%.0f alpha %d\n", cakes_draw_names[i].c_str(),
                    static_cast<double>(d.src.x), static_cast<double>(d.src.y), d.alpha);
    }
    std::printf("cakes clocks: belt %.3f, dispatch %.3f, %zu queued\n",
        static_cast<double>(cakes_transport.belt_time), static_cast<double>(cakes_transport.shipping_time),
        cakes_transport.shipping_pending.size());
}

// Entering the room starts a round.  ComfyCakesGame::NewGame is what PurblePlaceManager's
// Show path calls, and --cakes-difficulty picks the difficulty the way the chooser does.
void App::start_cakes(int difficulty) {
    hide_tip();
    free_direct(result_draws);
    result_buttons.clear();
    result_shown = false;
    rebuild_overlay();
    pairs_ready = false;
    picker_cakes = true;
    picker_choice = difficulty == 0 ? "Beginner" : (difficulty == 1 ? "Intermediate" : "Advanced");
    screen = Screen::ComfyCakes;
    menu_ready = false;
    cakes_ready = false;
    cakes_hover_name.clear();
    cakes_new_game(difficulty);
    build_cakes();
    cakes_refresh();
    if (cakes_solve_hook) {
        cakes_solve_hook = false; // one-shot: result-panel New Game must start a playable round
        cakes_solve();
    }
}

}  // namespace pple::viewer
