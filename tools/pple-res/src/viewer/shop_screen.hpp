// Purble Shop (Build-a-Purble) room records.  The documents this module reads are
// UI\BuildAPurble\BuildAPurbleGame.xml (BuildAPurbleGame::LoadGameUI 0x100050B54) and the two
// documents it points at: UI\BuildAPurble\Cabinet\Cabinet.xml (FeatureSet::Init 0x10006BC50 and
// Shelf::Init 0x100073C18) and one of the six UI\BuildAPurble\PurbleFaceTemplate*.xml files
// (Face::SetTemplate 0x100069444, Stage::SetTemplate 0x1000744AC).
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pple::viewer {

// One cabinet item: the button the player clicks, its own shelf's slot and its check box.
struct ShopButtonDef {
    std::string name;    // "RedEye2" (the cabinet's own name; the face calls it "RedEye2" too)
    std::string sprite;  // UI/BuildAPurble/ShelfFeatures/RedEyes2s.xml
    int color = 0;       // 0..4
    int feature = 0;     // 0..4, Hair..Extras
    int variant = 0;     // 0..2
    int x = 0, y = 0;             // the button node's own offset inside the slot
    int check_x = 0, check_y = 0; // the check box's offset inside the slot
};

// One shelf: a shelf sprite and the items standing on it.  The item list's values are each
// item's x inside the shelf (Shelf::Init reads them into the shelf's own array).
struct ShopShelfDef {
    std::string name;
    std::string sprite;
    int x = 0, y = 0;
    int items_y = 0;
    std::vector<std::pair<std::string, int>> items;
};

// One named element of UI\BuildAPurble\BuildAPurbleGame.xml, as BuildAPurbleGame::LoadGameUI
// reads it.  `<Desc><Button>` wins over `<Desc><Sprite>` for the guess button, whose document
// carries both: the <Button> is what an untouched node draws and the <Sprite> is its idle
// animation, hidden until GuessButton::OnIdleTimerFired shows it.
struct ShopNodeDef {
    std::string name;
    std::string sprite;  // <Desc><Sprite>
    std::string button;  // <Desc><Button>
    float x = 0.0f, y = 0.0f;
    float scale = 1.0f;
    bool has_scale = false;
};

// One of the cabinet's colour stones (RedStoneBeginner and friends).
struct ShopStoneDef {
    std::string name;
    std::string sprite;
    int x = 0, y = 0;
    int color = 0;
};

// One feature sprite of a face template, keyed by its name ("RedEye2").  `sound` is the
// template's own <SoundID>, a GameAudio proto (FaceFeatureSprite::Init 0x10006B590 reads it, and
// FaceFeatureSprite::Show 0x10006BA12 plays it when a player picks the item).
struct ShopFaceItemDef {
    std::string name;
    std::string sprite;
    int x = 0, y = 0;
    int sound = -1;
};

// A face template document: the mystery head, the body and head, and every feature sprite.
// `order` keeps the document's own order, because that is the order Face::SetTemplate creates
// its child nodes in and therefore the draw order.
struct ShopFaceTemplate {
    bool loaded = false;
    std::string document;
    std::string mystery_sprite;
    int mystery_x = 0, mystery_y = 0;
    std::string head_sprite, body_sprite;
    int head_x = 0, head_y = 0, body_x = 0, body_y = 0;
    // Stage::SetTemplate places the body and head on the stage from these instead of X/Y.
    int head_stage_x = 0, head_stage_y = -25, body_stage_x = 0, body_stage_y = -25;
    std::vector<std::string> order;
    std::map<std::string, ShopFaceItemDef> items;
};

}  // namespace pple::viewer
