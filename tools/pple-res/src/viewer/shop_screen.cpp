#include "app.hpp"
#include "audio.hpp"

#include <cstdlib>

namespace pple::viewer {
namespace {

int xml_int(const pple::XmlNode& node, const char* name, int fallback = 0) {
    const pple::XmlNode* child = node.child(name);
    if (child == nullptr || child->text.empty()) return fallback;
    return static_cast<int>(std::strtol(child->text.c_str(), nullptr, 10));
}

std::string xml_text(const pple::XmlNode& node, const char* name) {
    const pple::XmlNode* child = node.child(name);
    return child == nullptr ? std::string{} : child->text;
}

// "<Color>Stone<Difficulty>": the two names are the engine's own (FeatureSet::Init builds them
// from its colour and difficulty string tables).
bool parse_stone_name(const std::string& name, int& color, int& difficulty) {
    static const char* const kStones[5] = {"RedStone", "PurpleStone", "YellowStone", "BlueStone",
                                           "GreenStone"};
    static const char* const kDifficulty[3] = {"Beginner", "Intermediate", "Advanced"};
    for (int c = 0; c < 5; ++c) {
        if (name.rfind(kStones[c], 0) != 0) continue;
        const std::string rest = name.substr(std::string(kStones[c]).size());
        for (int d = 0; d < 3; ++d)
            if (rest == kDifficulty[d]) {
                color = c;
                difficulty = d;
                return true;
            }
    }
    return false;
}

// "Shelf<Feature><Difficulty>".
bool parse_shelf_name(const std::string& name, int& feature, int& difficulty) {
    if (name.rfind("Shelf", 0) != 0) return false;
    static const char* const kFeatures[5] = {"Hair", "Eye", "Nose", "Mouth", "Accessory"};
    static const char* const kDifficulty[3] = {"Beginner", "Intermediate", "Advanced"};
    const std::string rest = name.substr(5);
    for (int f = 0; f < 5; ++f) {
        const std::string head = kFeatures[f];
        if (rest.rfind(head, 0) != 0) continue;
        const std::string tail = rest.substr(head.size());
        for (int d = 0; d < 3; ++d)
            if (tail == kDifficulty[d]) {
                feature = f;
                difficulty = d;
                return true;
            }
    }
    return false;
}

// The rectangle a click tests: the node's own sprite box, (x, y) - registration and
// SourceWidth x SourceHeight.  PurblePlaceUILoader::LoadButton (0x1000737B0) reads no hit box of
// its own, and "OverrideBB" is not a string in either DLL, so the authored <OverrideBB> values
// are dead data in this build.
SDL_FRect node_rect(float x, float y, const Sprite& sprite, float scale) {
    return SDL_FRect{x - sprite.reg_x * scale, y - sprite.reg_y * scale, sprite.src.w * scale,
                     sprite.src.h * scale};
}

}  // namespace

// One sprite of the room at (x, y).  `frame` -1 means the state's own frame: frame 0 for a plain
// sprite, the last frame for a button's hover art, which is what NodeButton::HandleEvent resumes
// (0x1000AA985).
bool App::shop_append(const std::string& sprite_xml, float x, float y, float scale, int frame,
                      int state, const std::string& name, bool hotspot, int alpha) {
    if (sprite_xml.empty()) return false;
    const Sprite sprite = resolve_sprite(bundle, sprite_xml, state);
    if (!sprite.ok) return false;
    std::string sheet_how;
    const auto* slot = bundle.resolve(sprite.requested, sheet_how);
    if (slot == nullptr) return false;
    std::string note;
    SDL_Texture* tex = load_sheet_texture(sprite.requested, slot->entry->name, note);
    if (tex == nullptr) return false;
    pple::SceneItem geometry;
    geometry.src_x = sprite.src.x;
    geometry.src_y = sprite.src.y;
    geometry.src_w = sprite.src.w;
    geometry.src_h = sprite.src.h;
    geometry.frames = sprite.frames;
    geometry.per_row = sprite.per_row;
    geometry.prepped = sprite.prepped;
    const int want = frame >= 0 ? frame : (state == 0 ? 0 : sprite.frames - 1);
    float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
    pple::sprite_frame_rect(geometry, want, fx, fy, fw, fh);
    DrawItem d;
    d.texture = tex;
    d.src = SDL_FRect{fx, fy, fw, fh};
    d.dst = SDL_FRect{x - sprite.reg_x * scale, y - sprite.reg_y * scale, fw * scale,
                      fh * scale};
    d.alpha = alpha;
    scene_draws.push_back(d);
    if (!name.empty() && hotspot) shop_hotspot(name, node_rect(x, y, sprite, scale));
    scene_w = std::max(scene_w, d.dst.x + d.dst.w);
    scene_h = std::max(scene_h, d.dst.y + d.dst.h);
    return true;
}

void App::shop_hotspot(const std::string& name, const SDL_FRect& rect) {
    Hotspot spot;
    spot.name = name;
    spot.rect = rect;
    shop_hotspots.push_back(spot);
}

bool App::shop_append_button(const std::string& sprite_xml, const std::string& name, float x,
                             float y, float scale, int state) {
    return shop_append(sprite_xml, x, y, scale, -1, state, name);
}

const ShopFaceTemplate* App::shop_face() const {
    const auto it = shop_templates.find(shop_template_doc);
    return it == shop_templates.end() || !it->second.loaded ? nullptr : &it->second;
}

// The item the cabinet shows for one (colour, feature): the variant FeatureSet::Reset picked.
const ShopButtonDef* App::shop_item(int color, int feature) const {
    const int variant = shop_model.variant(color, feature);
    const std::string want = pple::shop_button_name(color, feature, variant);
    for (const ShopButtonDef& button : shop_buttons)
        if (button.name == want) return &button;
    return nullptr;
}

// Reads the documents the room needs.  BuildAPurbleGame::InitGame (0x10005194C) loads the six
// face templates per difficulty and InitGameUI loads the cabinet and the room document.
void App::shop_load_documents() {
    if (shop_documents_loaded) return;
    shop_documents_loaded = true;
    // The room document.  LoadGameUI looks every element up by name, so the module keeps the
    // document's own X/Y, scale and description per name.
    std::vector<std::uint8_t> data;
    if (read_resource(shop_xml, data)) {
        pple::XmlNode root;
        std::string error;
        if (pple::xml_parse(data, root, error)) {
            for (const pple::XmlNode& child : root.children) {
                const std::string name = child.attr("Name");
                if (name.empty()) continue;
                ShopNodeDef element;
                element.name = name;
                element.x = static_cast<float>(xml_int(child, "X"));
                element.y = static_cast<float>(xml_int(child, "Y"));
                if (const pple::XmlNode* scale = child.child("scale")) {
                    element.has_scale = true;
                    element.scale = static_cast<float>(std::atof(scale->text.c_str())) / 100.0f;
                } else if (const pple::XmlNode* scale = child.child("Scale")) {
                    element.has_scale = true;
                    element.scale = static_cast<float>(std::atof(scale->text.c_str())) / 100.0f;
                }
                if (const pple::XmlNode* desc = child.child("Desc")) {
                    element.sprite = xml_text(*desc, "Sprite");
                    element.button = xml_text(*desc, "Button");
                }
                shop_nodes[name] = element;
            }
        } else {
            std::fprintf(stderr, "shop: %s: %s\n", shop_xml.c_str(), error.c_str());
        }
    } else {
        std::fprintf(stderr, "shop: %s not found\n", shop_xml.c_str());
    }
    std::vector<std::uint8_t> cabinet;
    if (!read_resource(shop_cabinet_xml, cabinet)) {
        std::fprintf(stderr, "shop: %s not found\n", shop_cabinet_xml.c_str());
        return;
    }
    pple::XmlNode root;
    std::string error;
    if (!pple::xml_parse(cabinet, root, error)) {
        std::fprintf(stderr, "shop: cabinet parse: %s\n", error.c_str());
        return;
    }
    // The buttons and shelves are found by the engine's own generated names, so the document's
    // spelling of each name never has to be parsed.
    std::map<std::string, const pple::XmlNode*> by_name;
    for (const pple::XmlNode& child : root.children) {
        const std::string name = child.attr("Name");
        if (!name.empty()) by_name[name] = &child;
    }
    for (int color = 0; color < 5; ++color)
        for (int feature = 0; feature < 5; ++feature)
            for (int variant = 0; variant < 3; ++variant) {
                const auto found = by_name.find(pple::shop_button_name(color, feature, variant));
                if (found == by_name.end()) continue;
                const pple::XmlNode& node = *found->second;
                ShopButtonDef button;
                button.name = found->first;
                button.color = color;
                button.feature = feature;
                button.variant = variant;
                button.x = xml_int(node, "X");
                button.y = xml_int(node, "Y");
                button.check_x = xml_int(node, "CheckBoxX");
                button.check_y = xml_int(node, "CheckBoxY", 20);
                const pple::XmlNode* desc = node.child("Desc");
                if (desc != nullptr) {
                    const pple::XmlNode* sprite = desc->child("Button");
                    if (sprite == nullptr) sprite = desc->child("Sprite");
                    if (sprite != nullptr) button.sprite = sprite->text;
                }
                shop_buttons.push_back(button);
            }
    for (const pple::XmlNode& child : root.children) {
        if (child.name != "NodeSprite") continue;
        int color = 0, difficulty = 0;
        if (!parse_stone_name(child.attr("Name"), color, difficulty)) continue;
        ShopStoneDef stone;
        stone.name = child.attr("Name");
        stone.color = color;
        stone.x = xml_int(child, "X");
        stone.y = xml_int(child, "Y");
        const pple::XmlNode* desc = child.child("Desc");
        if (desc != nullptr) stone.sprite = xml_text(*desc, "Sprite");
        shop_stones.push_back(stone);
    }
    for (const pple::XmlNode& child : root.children) {
        if (child.name != "Shelf") continue;
        int feature = 0, difficulty = 0;
        if (!parse_shelf_name(child.attr("Name"), feature, difficulty)) continue;
        ShopShelfDef shelf;
        shelf.name = child.attr("Name");
        shelf.x = xml_int(child, "X");
        shelf.y = xml_int(child, "Y");
        const pple::XmlNode* desc = child.child("Desc");
        if (desc == nullptr) continue;
        shelf.items_y = xml_int(*desc, "ItemsY");
        shelf.sprite = xml_text(*desc, "Sprite");
        const pple::XmlNode* list = desc->child("ItemList");
        if (list != nullptr)
            for (const pple::XmlNode& item : list->children)
                shelf.items.emplace_back(
                    item.name, static_cast<int>(std::strtol(item.text.c_str(), nullptr, 10)));
        shop_shelves.push_back(shelf);
    }
}

void App::start_shop(int difficulty) {
    shop_difficulty = std::clamp(difficulty, 0, 2);
    shop_load_documents();
    // Every shop round needs the shared device and the room's own events; the feature sounds
    // themselves decode on first use.
    initialise_shop_audio(bundle, mute);
    shop_new_game(shop_difficulty);
    std::printf("shop: Purble Shop %s\n", pple::kShopCounts[shop_difficulty] == 3 ? "Beginner"
                                      : pple::kShopCounts[shop_difficulty] == 4 ? "Intermediate"
                                                                                : "Advanced");
}

// BuildAPurbleGame::NewGame (0x100052528): a template, the cabinet's variants, the hidden face,
// the counters and the curtain.
void App::shop_new_game(int difficulty) {
    shop_difficulty = std::clamp(difficulty, 0, 2);
    shop_model.new_game(shop_difficulty, shop_seed);
    shop_reveal = false;
    shop_curtain_frame = 7;  // the intro opens the curtain; the room rests open
    shop_history_first = 0;
    for (int color = 0; color < 5; ++color)
        for (int feature = 0; feature < 5; ++feature)
            // FeatureSet::Reset's own default: on Beginner the box is not drawn at all, on the
            // other two the items carry an empty box the player can tag.
            shop_check[color][feature] = shop_difficulty == 0 ? 0 : 1;
    shop_load_template();
    // BuildAPurbleGame::NewGame closes the curtain and the manager's state 0 then runs the
    // curtain intro, which is proto 0x45 (BAP_FEATURE_INTRO).  The close itself is the
    // IntroAnimation's "PlayCloseSound" step, proto 0x58.
    play_shop_sfx("BAP_CURTAIN_CLOSE");
    play_shop_sfx("BAP_FEATURE_INTRO");
    std::printf("shop: new %s round, seed %u, template %d, hidden %d %d %d %d %d\n",
                shop_difficulty == 0 ? "Beginner" : shop_difficulty == 1 ? "Intermediate"
                                                                        : "Advanced",
                shop_seed, shop_model.template_id(), shop_model.hidden(0), shop_model.hidden(1),
                shop_model.hidden(2), shop_model.hidden(3), shop_model.hidden(4));
    shop_refresh();
}

// ApplyTemplateToAllFaces (0x100054BB8) hands the same document to the stage, the curtain, both
// faces and the history window.
void App::shop_load_template() {
    static const char* const kTemplates[6] = {
        "UI\\BUILDAPURBLE\\PURBLEFACETEMPLATE1.XML", "UI\\BUILDAPURBLE\\PURBLEFACETEMPLATE2.XML",
        "UI\\BUILDAPURBLE\\PURBLEFACETEMPLATE4.XML", "UI\\BUILDAPURBLE\\PURBLEFACETEMPLATE5.XML",
        "UI\\BUILDAPURBLE\\PURBLEFACETEMPLATE7.XML", "UI\\BUILDAPURBLE\\PURBLEFACETEMPLATE8.XML",
    };
    const int id = std::clamp(shop_model.template_id(), 0, 5);
    shop_template_doc = kTemplates[id];
    if (shop_templates.count(shop_template_doc) != 0) return;
    ShopFaceTemplate face;
    face.document = shop_template_doc;
    std::vector<std::uint8_t> data;
    if (read_resource(shop_template_doc, data)) {
        pple::XmlNode root;
        std::string error;
        if (pple::xml_parse(data, root, error)) {
            const pple::XmlNode* mystery = root.child("MysteryHead");
            if (mystery != nullptr) {
                face.mystery_x = xml_int(*mystery, "X");
                face.mystery_y = xml_int(*mystery, "Y");
                const pple::XmlNode* desc = mystery->child("Desc");
                if (desc != nullptr) face.mystery_sprite = xml_text(*desc, "Sprite");
            }
            const pple::XmlNode* features = root.child("Features");
            if (features != nullptr) {
                for (const pple::XmlNode& child : features->children) {
                    const pple::XmlNode* desc = child.child("Desc");
                    if (desc == nullptr) continue;
                    const pple::XmlNode* sprite = desc->child("Sprite");
                    if (sprite == nullptr) continue;
                    if (child.name == "Body") {
                        face.body_sprite = sprite->text;
                        face.body_x = xml_int(child, "X");
                        face.body_y = xml_int(child, "Y");
                        face.body_stage_x = xml_int(child, "StageX");
                        face.body_stage_y = xml_int(child, "StageY", -25);
                        face.order.push_back("Body");
                        continue;
                    }
                    if (child.name == "Head") {
                        face.head_sprite = sprite->text;
                        face.head_x = xml_int(child, "X");
                        face.head_y = xml_int(child, "Y");
                        face.head_stage_x = xml_int(child, "StageX");
                        face.head_stage_y = xml_int(child, "StageY", -25);
                        face.order.push_back("Head");
                        continue;
                    }
                    ShopFaceItemDef item;
                    item.name = child.name;
                    item.sprite = sprite->text;
                    item.x = xml_int(child, "X");
                    item.y = xml_int(child, "Y");
                    // The template's <SoundID> sits inside the <Desc> with the sprite.
                    if (const pple::XmlNode* sound = desc->child("SoundID"))
                        item.sound = static_cast<int>(std::strtol(sound->text.c_str(), nullptr, 10));
                    face.order.push_back(item.name);
                    face.items[item.name] = item;
                }
            }
            face.loaded = !face.body_sprite.empty() && !face.head_sprite.empty();
        } else {
            std::fprintf(stderr, "shop: %s: %s\n", shop_template_doc.c_str(), error.c_str());
        }
    }
    shop_templates[shop_template_doc] = face;
}

// One purble: the body, the head and the feature sprites of `colors`, drawn in the template
// document's own order because that is the order Face::SetTemplate creates its nodes in (the
// extras sit behind the head, the hair in front of it).  A colour of -1 leaves the slot empty,
// which is what Face::SetFeatureColors does with a -1.
void App::shop_draw_face(const ShopFaceTemplate& face, float base_x, float base_y, float scale,
                         const int* colors, bool with_features) {
    for (const std::string& name : face.order) {
        if (name == "Body") {
            shop_append(face.body_sprite, base_x + face.body_x * scale,
                        base_y + face.body_y * scale, scale);
            continue;
        }
        if (name == "Head") {
            shop_append(face.head_sprite, base_x + face.head_x * scale,
                        base_y + face.head_y * scale, scale);
            continue;
        }
        if (!with_features) continue;
        for (int slot = 0; slot < 5; ++slot) {
            const int color = colors[slot];
            if (color < 0) continue;
            if (name != pple::shop_face_item_name(color, slot, shop_model.variant(color, slot)))
                continue;
            const auto item = face.items.find(name);
            if (item == face.items.end()) continue;
            shop_append(item->second.sprite, base_x + item->second.x * scale,
                        base_y + item->second.y * scale, scale);
        }
    }
}

// The cabinet: the colour stones of the current difficulty, then that difficulty's shelves and,
// on each shelf, the one visible variant of every colour the difficulty uses.
void App::shop_draw_cabinet() {
    for (const ShopStoneDef& stone : shop_stones) {
        if (stone.color >= shop_model.count()) continue;
        shop_append(stone.sprite, static_cast<float>(stone.x), static_cast<float>(stone.y));
    }
    for (const ShopShelfDef& shelf : shop_shelves) {
        int feature = 0, difficulty = 0;
        if (!parse_shelf_name(shelf.name, feature, difficulty)) continue;
        if (difficulty != shop_difficulty) continue;
        if (!shop_model.feature_in_play(feature)) continue;
        shop_append(shelf.sprite, static_cast<float>(shelf.x), static_cast<float>(shelf.y));
        for (const auto& [button_name, item_x] : shelf.items) {
            const ShopButtonDef* button = nullptr;
            for (const ShopButtonDef& candidate : shop_buttons)
                if (candidate.name == button_name) {
                    button = &candidate;
                    break;
                }
            if (button == nullptr) continue;
            if (button->color >= shop_model.count()) continue;
            // Only the variant this round picked is visible (FeatureSet::Reset's own test).
            if (button->variant != shop_model.variant(button->color, button->feature)) continue;
            // Shelf::Arrange places the item's container at the shelf's own origin plus the
            // item list's x and the shelf's ItemsY; the button and its check box hang off that
            // container at their own offsets.
            const float slot_x = static_cast<float>(shelf.x + item_x);
            const float slot_y = static_cast<float>(shelf.y + shelf.items_y);
            const float x = slot_x + static_cast<float>(button->x);
            const float y = slot_y + static_cast<float>(button->y);
            const bool hovered = button->name == shop_hover_name;
            // The button is drawn first (the check box sits over it) but the check box wins the
            // click where they overlap: the engine hit-tests the topmost node and
            // FaceFeatureButton::Init (0x10006A434) creates the box after the button.
            shop_append(button->sprite, x, y, 1.0f, -1, hovered ? 1 : 0, std::string(), false);
            // The check box: state 0 shows nothing, 1 the empty box, 2 the green check and
            // 3 the red X (FaceFeatureCheckBox::SetState).
            const int state = shop_check[button->color][button->feature];
            if (state != 0) {
                const float cx = slot_x + static_cast<float>(button->check_x);
                const float cy = slot_y + static_cast<float>(button->check_y);
                shop_append("UI/BuildAPurble/Cabinet/CheckBox.xml", cx, cy, 1.0f, 0, 0,
                            std::string(), false);
                if (state == 2)
                    shop_append("UI/BuildAPurble/Cabinet/GreenCheck.xml", cx, cy, 1.0f, 0, 0,
                                std::string(), false);
                else if (state == 3)
                    shop_append("UI/BuildAPurble/Cabinet/RedX.xml", cx, cy, 1.0f, 0, 0,
                                std::string(), false);
            }
            // The check box is a NodeButton of its own, created after the item's (so it is both
            // drawn over it and hit-tested first): its hot-spot goes in before the item's.
            const Sprite art = resolve_sprite(bundle, button->sprite, hovered ? 1 : 0);
            if (state != 0) {
                const Sprite box = resolve_sprite(bundle, "UI/BuildAPurble/Cabinet/CheckBox.xml", 0);
                shop_hotspot(button->name + "/CheckBox",
                             node_rect(slot_x + static_cast<float>(button->check_x),
                                       slot_y + static_cast<float>(button->check_y), box, 1.0f));
            }
            shop_hotspot(button->name, node_rect(x, y, art, 1.0f));
        }
    }
}

// The history window's slots.  HistoryWindow::Init (0x10006DEBC) builds six slots from
// HISTORYWINDOW.XML's own <HistorySlot> X/Y; each slot's contents are hidden until a guess
// occupies it (HistorySlotWidget::SetContentsVisible).
void App::shop_draw_history() {
    std::vector<std::uint8_t> data;
    if (!read_resource("UI\\BUILDAPURBLE\\HISTORYWINDOW\\HISTORYWINDOW.XML", data)) return;
    pple::XmlNode root;
    std::string error;
    if (!pple::xml_parse(data, root, error)) return;
    std::size_t shown = 0;
    const std::vector<pple::ShopGuess>& history = shop_model.history();
    // The thermometer is the *guess* number, not the score: HistoryWindow::UpdateSlots sets its
    // frame to (entry index * last frame) / the difficulty's guess limit, and that limit is
    // MaxGuessesINT (7) on Beginner and Intermediate and MaxGuessesADV (13) on Advanced.
    const int limit = shop_difficulty == 2 ? 13 : 7;
    // HistoryWindow+4 is the first entry the strip shows; AddEntry parks it at count - 6 and the
    // arrows move it by one.
    const std::size_t offset = shop_history_first < 0 ? 0
                               : static_cast<std::size_t>(shop_history_first) > history.size()
                                   ? history.size()
                                   : static_cast<std::size_t>(shop_history_first);
    for (const pple::XmlNode& slot : root.children) {
        if (slot.name != "HistorySlot") continue;
        const float x = static_cast<float>(xml_int(slot, "X"));
        const float y = static_cast<float>(xml_int(slot, "Y"));
        const std::size_t index = offset + shown;
        ++shown;
        if (index >= history.size()) continue;
        const pple::ShopGuess& entry = history[index];
        const int frame = static_cast<int>((index * 12) / static_cast<std::size_t>(limit));
        shop_append("UI/BuildAPurble/HistoryWindow/Thermometer.xml", x + 3.0f, y - 2.0f, 1.0f,
                    frame);
        const ShopFaceTemplate* face = shop_face();
        if (face != nullptr)
            shop_draw_face(*face, x + 30.0f, y + 25.0f, 0.35f, entry.colors.data(), true);
        shop_append("UI/BuildAPurble/GreenNumbers.xml", x + 105.0f, y + 47.0f, 0.5f, entry.rcrf);
        if (shop_difficulty == 2)
            shop_append("UI/BuildAPurble/RedNumbers.xml", x + 105.0f, y + 70.0f, 0.5f, entry.rcwf);
    }
    // The two arrows are ghosted (alpha 32, HistoryWindow::SetGhosted 0x10006EAFC, reached from
    // UpdateSlots' own tail) when there is nothing to scroll that way: both when the strip holds
    // six guesses or fewer, the left one at the start of the window and the right one at its end.
    const bool too_few = history.size() <= 6;
    const bool left_ghost = too_few || offset == 0;
    const bool right_ghost = too_few || offset + 6 >= history.size();
    shop_append("UI/BuildAPurble/HistoryWindow/GreenArrowSpriteLeft.xml", 36.0f, 393.0f, 1.0f, -1,
                0, "LeftHistoryButton", true, left_ghost ? 32 : 255);
    shop_append("UI/BuildAPurble/HistoryWindow/GreenArrowSpriteRight.xml", 741.0f, 393.0f, 1.0f,
                -1, 0, "RightHistoryButton", true, right_ghost ? 32 : 255);
}

// HistoryWindow::HandleEvent's arrow branches: the left arrow (this+10, "Scroll Left") walks the
// window down towards older guesses and plays proto 4, the right one (this+11) towards newer ones
// and plays proto 5.
void App::shop_history_slide(int direction) {
    const int count = static_cast<int>(shop_model.history().size());
    const int last = count > 6 ? count - 6 : 0;
    if (direction < 0) {
        if (shop_history_first <= 0) return;
        --shop_history_first;
        play_shop_sfx("LEFT_BUTTON");
    } else {
        if (shop_history_first >= last) return;
        ++shop_history_first;
        play_shop_sfx("RIGHT_BUTTON");
    }
    std::printf("shop: history window shows %d..%d of %d\n", shop_history_first + 1,
                shop_history_first + 6, count);
    shop_refresh();
}

// A click on an item's check box: FaceFeatureCheckBox::HandleEvent's own cycle, which exists only
// on Intermediate and Advanced (`difficulty != 0`) and only while the item is not locked.
void App::shop_tag(int feature, int color) {
    if (shop_difficulty == 0) return;
    if (feature < 0 || feature >= 5 || color < 0 || color >= 5) return;
    int& state = shop_check[color][feature];
    state = state == 1 ? 3 : state == 3 ? 2 : state == 2 ? 1 : 1;
    std::printf("shop: tagged %s %s as %s\n", pple::shop_color_name(color),
                pple::shop_feature_name(feature),
                state == 2 ? "right" : state == 3 ? "wrong" : "untagged");
    shop_refresh();
}

// The colour-count widget (ColorCountWidget::SetCount 0x100059470): Beginner and Intermediate
// show one count, Advanced two (the second is the right-colour wrong-feature count).  The
// numbers and their labels come from UI\BuildAPurble\ColorCountTemplate.xml, whose RCRF/RCWF
// entries ColorCountWidget::Init creates and SetCount shows or hides.
void App::shop_draw_counts() {
    if (shop_model.guesses() == 0) return;
    const float x = 252.0f, y = 85.0f;
    std::vector<std::uint8_t> data;
    pple::XmlNode root;
    if (read_resource("UI\\BUILDAPURBLE\\COLORCOUNTTEMPLATE.XML", data)) {
        std::string error;
        if (!pple::xml_parse(data, root, error)) root.children.clear();
    }
    const auto label = [&](const char* name, float ox, float oy) {
        const pple::XmlNode* element = root.child(name);
        if (element == nullptr) return;
        const pple::XmlNode* desc = element->child("Desc");
        if (desc == nullptr) return;
        pple::SceneItem item;
        item.x = x + ox + static_cast<float>(xml_int(*element, "X"));
        item.y = y + oy + static_cast<float>(xml_int(*element, "Y"));
        item.w = static_cast<float>(xml_int(*element, "W"));
        item.h = static_cast<float>(xml_int(*element, "H"));
        item.text = pple::localized_default(xml_text(*desc, "Text"));
        item.font_size = static_cast<float>(std::atof(xml_text(*desc, "FontSize").c_str()));
        item.colour = static_cast<std::uint32_t>(
            std::strtoul(xml_text(*desc, "Color").c_str(), nullptr, 16));
        item.justify = xml_int(*desc, "Justify");
        if (!item.text.empty()) append_label(item);
    };
    if (shop_difficulty == 2) {
        shop_append("UI/BuildAPurble/GreenNumbers.xml", x + 10.0f, y + 25.0f, 1.0f,
                    shop_model.rcrf());
        shop_append("UI/BuildAPurble/RedNumbers.xml", x + 10.0f, y + 70.0f, 1.0f,
                    shop_model.rcwf());
        label("RCRFLabel", 0.0f, 0.0f);
        label("RCWFLabel", 0.0f, 0.0f);
    } else {
        shop_append("UI/BuildAPurble/GreenNumbers.xml", x + 76.0f, y + 20.0f, 1.0f,
                    shop_model.rcrf());
        label("RCRFLabelSimple", 0.0f, 0.0f);
    }
}

// The room.  BuildAPurbleGame::LoadGameUI's own order, with the two faces at the end because
// their features are created after every other node.  The visible set is the state a round is
// played in: the intro has opened the curtain and Stage::OnIntroAnimComplete (0x100074368) has
// hidden the stage and shown the guess purble, while the hidden purble's own node stays hidden
// behind the mystery head until the win or the loss shows it.
void App::build_shop() {
    release_draws();
    shop_hotspots.clear();
    scene_w = scene_h = 0.0f;
    shop_ready = true;
    screen = Screen::Shop;
    const auto node = [this](const char* name) -> const ShopNodeDef* {
        const auto it = shop_nodes.find(name);
        return it == shop_nodes.end() ? nullptr : &it->second;
    };
    if (const ShopNodeDef* back = node("Background")) shop_append(back->sprite, back->x, back->y);
    const ShopFaceTemplate* face = shop_face();
    const ShopNodeDef* curtain = node("Curtain");
    // Curtain::Init (0x100063658) creates the curtain's own children as children of the *same*
    // parent node the curtain sprite is created in: the widget's node is not itself moved, so
    // the head container sits at the document's <HeadX>/<HeadY> (75, 70) and the question mark
    // at (101, 102), both in room coordinates.  The mystery head then covers exactly where the
    // 42%-scale hidden purble's head is (86.9, 96.9).
    if (face != nullptr && !shop_reveal) {
        shop_append(face->mystery_sprite, 75.0f + static_cast<float>(face->mystery_x),
                    70.0f + static_cast<float>(face->mystery_y), 1.0f);
    }
    if (const ShopNodeDef* rod = node("CurtainRod")) shop_append(rod->sprite, rod->x, rod->y);
    if (curtain != nullptr) {
        shop_append(curtain->sprite, curtain->x, curtain->y, 1.0f, shop_curtain_frame);
        // The question mark hangs in the arch while the curtain is closed; the round's intro
        // opens the curtain, so it is not drawn over an open one.
        if (shop_curtain_frame == 15)
            shop_append("UI/BuildAPurble/question_mark.xml", 101.0f, 102.0f, 1.0f, 0);
    }
    if (const ShopNodeDef* spot = node("Spotlight")) shop_append(spot->sprite, spot->x, spot->y);
    if (const ShopNodeDef* spot = node("Spotlight2")) shop_append(spot->sprite, spot->x, spot->y);
    shop_draw_cabinet();
    if (const ShopNodeDef* guess = node("GuessButton")) {
        // GuessButton::Init (0x10006D4A8) builds the NodeButton from <Button> and the idle
        // animation sprite from <Sprite>; the sprite starts hidden.
        shop_append(guess->button.empty() ? guess->sprite : guess->button, guess->x, guess->y, 1.0f,
                    -1, 0, "GuessButton");
    }
    shop_draw_history();
    shop_draw_counts();
    // The guess purble is the one the player assembles.
    if (face != nullptr) {
        float gx = 129.0f, gy = 210.0f, gscale = 1.0f;
        if (const ShopNodeDef* guess = node("GuessPurble")) {
            gx = guess->x;
            gy = guess->y;
            if (guess->has_scale) gscale = guess->scale;
        }
        int colors[5];
        for (int slot = 0; slot < 5; ++slot) colors[slot] = shop_model.selected(slot);
        shop_draw_face(*face, gx, gy, gscale, colors, true);
    }
    // The reveal after a win or a loss: the hidden purble's own node.
    if (shop_reveal && face != nullptr) {
        float hx = 97.0f, hy = 120.0f, hscale = 0.42f;
        if (const ShopNodeDef* hidden = node("HiddenPurble")) {
            hx = hidden->x;
            hy = hidden->y;
            if (hidden->has_scale) hscale = hidden->scale;
        }
        int colors[5];
        for (int slot = 0; slot < 5; ++slot) colors[slot] = shop_model.hidden(slot);
        shop_draw_face(*face, hx, hy, hscale, colors, true);
    }
    if (const ShopNodeDef* back = node("ReturnToMainMenu"))
        shop_append_button(back->button.empty() ? back->sprite : back->button, "ReturnToMainMenu",
                           back->x, back->y);
}

// The whole screen, from the same entry point for a rebuild, a click and a hover.
void App::shop_refresh() {
    build_shop();
    if (shop_dump)
        for (const Hotspot& spot : shop_hotspots)
            std::printf("  shop spot %-18s %.0f,%.0f %.0fx%.0f\n", spot.name.c_str(),
                        static_cast<double>(spot.rect.x), static_cast<double>(spot.rect.y),
                        static_cast<double>(spot.rect.w), static_cast<double>(spot.rect.h));
    hud_lines = {
        std::string("Purble Shop - ") + (shop_difficulty == 0 ? "Beginner"
                                              : shop_difficulty == 1 ? "Intermediate" : "Advanced"),
        "guesses " + std::to_string(shop_model.guesses()) +
            (shop_model.max_guesses() != 0 ? "/" + std::to_string(shop_model.max_guesses()) : "") +
            ", score " + std::to_string(shop_model.score()) +
            (shop_model.over() ? (shop_model.won() ? ", won" : ", lost") : ""),
    };
    rebuild_hud();
}

// A click on a visible cabinet item: BuildAPurbleGame::OnSetFeature (0x100053C5C) toggles the
// slot's colour and hides the counts again.  The item's own <SoundID> plays when the feature is
// *shown* (FaceFeatureSprite::Show's `a2 == 1` branch, which is OnSetFeature's call), so a click
// that clears the slot is silent.
void App::shop_select(int feature, int color) {
    const int before = shop_model.selected(feature);
    shop_model.select(feature, color);
    if (shop_model.selected(feature) != before && shop_model.selected(feature) != -1) {
        const ShopFaceTemplate* face = shop_face();
        if (face != nullptr) {
            const auto item = face->items.find(
                pple::shop_face_item_name(color, feature, shop_model.variant(color, feature)));
            if (item != face->items.end() && item->second.sound >= 0)
                play_shop_sfx(audio_event_at(bundle, item->second.sound));
        }
    }
    shop_refresh();
}

void App::shop_guess() {
    const pple::ShopModel::Outcome outcome = shop_model.make_guess();
    if (!outcome.accepted) {
        // MakeGuess plays proto 3, BAD_MOVE, when the shelves are not all set; a repeat of a
        // guess already made only raises the "AlreadyMadeGuess" tip.
        std::printf("shop: guess refused (%s)\n",
                    outcome.duplicate ? "the same guess was already made" : "not every shelf is set");
        if (!outcome.duplicate) play_shop_sfx("BAD_MOVE");
        return;
    }
    // The guess face inflates its features as they are judged (proto 8, BAP_FACE_INFLATE).
    play_shop_sfx("BAP_FACE_INFLATE");
    // The Beginner hint: the shelves whose colour was right are marked green/red for every item,
    // and the wrong shelves mark the guessed item red (MakeGuess 0x100052B02's own loop).
    if (shop_difficulty == 0) {
        for (int slot = 0; slot < 5; ++slot) {
            if (!shop_model.feature_in_play(slot)) continue;
            const int guessed = shop_model.selected(slot);
            for (int color = 0; color < shop_model.count(); ++color)
                shop_check[color][slot] = shop_model.matched()[slot]
                                              ? (color == guessed ? 2 : 3)
                                              : (color == guessed ? 3 : 0);
        }
    }
    std::printf("shop: guess %d -> right colour/right feature %d, right colour/wrong feature %d, "
                "score %d\n",
                shop_model.guesses(), shop_model.rcrf(), shop_model.rcwf(), shop_model.score());
    // HistoryWindow::AddEntry parks the window on the newest entry once there are more than six.
    const int entries = static_cast<int>(shop_model.history().size());
    if (entries > 6) shop_history_first = entries - 6;
    if (outcome.won) {
        std::printf("shop: won in %d guess(es)\n", shop_model.guesses());
        shop_reveal = true;
        // OnEnterWonGameState plays proto 0x51 (BAP_FEATURE_WIN) and opens the curtain; the
        // "you won" event itself is proto 6, which OnWinAnimStop plays with the dialog.
        play_shop_sfx("BAP_FEATURE_WIN");
        play_shop_sfx("BAP_CURTAIN_WIN_OPEN");
    } else if (outcome.lost) {
        std::printf("shop: lost after %d guess(es)\n", shop_model.guesses());
        shop_reveal = true;
        // OnEnterLoseGameState: proto 2, LOSE_GAME, then the curtain opens on the answer.
        play_shop_sfx("LOSE_GAME");
        play_shop_sfx("BAP_CURTAIN_WIN_OPEN");
    }
    shop_refresh();
    if (outcome.won || outcome.lost) {
        // The shipped build puts up its own dialog here (OnWinAnimStop 0x100053A4C plays proto 6
        // first); the viewer uses the same end-of-round panel the other two games do.
        if (outcome.won) play_shop_sfx("BAP_WIN_GAME");
        build_result_panel(outcome.won);
    }
}

}  // namespace pple::viewer
