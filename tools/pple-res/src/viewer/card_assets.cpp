#include "app.hpp"

namespace pple::viewer {

int App::build_pair_card_assets() {
    const float card_scale = static_cast<float>(pairs_board.slots().front().scale_percent) / 100.0f;
    int failed = 0;
    card_cache.clear();
    const auto build_card_sprite = [&](const std::string& xml, SDL_Texture** tex_out,
                                       SDL_FRect& src_out, float& fw_out, float& fh_out,
                                       int* frames_out = nullptr, float* reg_x_out = nullptr,
                                       float* reg_y_out = nullptr,
                                       pple::SceneItem* geom_out = nullptr) {
        const Sprite sprite = resolve_sprite(bundle, xml, 0);
        if (!sprite.ok) return false;
        if (frames_out != nullptr) *frames_out = sprite.frames;
        if (reg_x_out != nullptr) *reg_x_out = sprite.reg_x;
        if (reg_y_out != nullptr) *reg_y_out = sprite.reg_y;
        std::string how;
        const auto* sheet_slot = bundle.resolve(sprite.requested, how);
        if (sheet_slot == nullptr) return false;
        SDL_Texture* tex = load_sheet_texture(sprite.requested, sheet_slot->entry->name, pairs_sheet_note);
        if (tex == nullptr) return false;
        pple::SceneItem geometry;
        geometry.src_x = sprite.src.x;
        geometry.src_y = sprite.src.y;
        geometry.src_w = sprite.src.w;
        geometry.src_h = sprite.src.h;
        geometry.frames = sprite.frames;
        geometry.per_row = sprite.per_row;
        geometry.prepped = sprite.prepped;
        if (geom_out != nullptr) *geom_out = geometry;
        float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
        pple::sprite_frame_rect(geometry, 0, fx, fy, fw, fh);
        *tex_out = tex;
        src_out = SDL_FRect{fx, fy, fw, fh};
        fw_out = fw;
        fh_out = fh;
        return true;
    };
    SDL_Texture* outline_tex = nullptr;
    SDL_FRect outline_src{};
    float outline_w = 0.0f, outline_h = 0.0f;
    build_card_sprite(pairs_outline, &outline_tex, outline_src, outline_w, outline_h);
    // Card+0xC8's glow, 150x150 against the card's own 144x150, drawn over the card face
    // (Card::InitUI creates it after FaceAnimation) at the card's x - 4.
    SDL_Texture* glow_tex = nullptr;
    SDL_FRect glow_src{};
    float glow_w = 0.0f, glow_h = 0.0f;
    build_card_sprite(pairs_glow_xml, &glow_tex, glow_src, glow_w, glow_h);
    pairs_glow_tex = glow_tex;
    pairs_glow_src = glow_src;
    pairs_glow_w = glow_w * card_scale;
    pairs_glow_h = glow_h * card_scale;
    // Card+0xB0 (the BlankCard a matched cell shows) and Card+0x110's sparks.
    build_card_sprite(pairs_blank_xml, &pairs_blank_tex, pairs_blank_src, pairs_blank_w,
                      pairs_blank_h, nullptr, &pairs_blank_reg_x, &pairs_blank_reg_y);
    build_card_sprite(pairs_spark_xml, &pairs_spark_tex, pairs_spark_src, pairs_spark_w,
                      pairs_spark_h, nullptr, &pairs_spark_reg_x, &pairs_spark_reg_y);
    // Card+0xA8's SneakIcon.  Its frame is 37x37 at registration (-1,-6), so on the card it is
    // drawn at the card's Base + (1, 6) * card scale.
    build_card_sprite(pairs_sneak_icon_xml, &pairs_sneak_icon_tex, pairs_sneak_icon_src,
                      pairs_sneak_icon_w, pairs_sneak_icon_h, nullptr, &pairs_sneak_icon_reg_x,
                      &pairs_sneak_icon_reg_y);
    // Card+0xC0 / Card+0xE8 / Card+0xF0: the flip's shadow and the matched card's two glints.
    build_card_sprite(pairs_shadow_xml, &pairs_shadow_tex, pairs_shadow_src, pairs_shadow_w,
                      pairs_shadow_h);
    build_card_sprite(pairs_tileglow_xml, &pairs_tileglow_tex, pairs_tileglow_src,
                      pairs_tileglow_w, pairs_tileglow_h);
    build_card_sprite(pairs_tilecover_xml, &pairs_tilecover_tex, pairs_tilecover_src,
                      pairs_tilecover_w, pairs_tilecover_h);
    // The two sprites the flying coin's emitters throw: Emitter_gold uses SparkleSprite_y and
    // Emitter_white uses SparkleSprite (ArcBehavior::Init 0x100083F98 / 0x100083FBE).
    build_card_sprite("UI\\Concentration\\SparkleSprite_y.xml", &pairs_spark_gold_tex,
                      pairs_spark_gold_src, pairs_spark_w, pairs_spark_h, nullptr,
                      &pairs_spark_reg_x, &pairs_spark_reg_y);
    build_card_sprite("UI\\Concentration\\SparkleSprite.xml", &pairs_spark_white_tex,
                      pairs_spark_white_src, pairs_spark_w, pairs_spark_h, nullptr,
                      &pairs_spark_reg_x, &pairs_spark_reg_y);
    // The bonus tile's movie.  Card::InitUI resolves the scene document's /BonusTileAnimation
    // through ConUILoader::LoadMovie (0x10007BEF6); the document is per-screen, so it is loaded
    // once and kept.
    if (!pairs_bonus_movie_ok) {
        std::string scene_how;
        const auto* scene_slot = bundle.resolve(scene_xml, scene_how);
        if (scene_slot != nullptr) {
            pple::XmlNode root;
            std::string xml_error;
            if (pple::xml_parse(bundle.read(*scene_slot), root, xml_error)) {
                if (const auto* node = root.find("BonusTileAnimation")) {
                    if (const auto* desc = node->find("Desc")) {
                        const std::string name = desc->child_text("Movie");
                        std::vector<std::uint8_t> bytes;
                        if (!name.empty() && fetch_resource(name, bytes)) {
                            pairs_bonus_movie_ok = pple::movie_load(bytes, fetch_resource,
                                                                    pairs_bonus_movie, xml_error);
                            std::printf("  bonus tile: %s, %d frame(s), %zu part(s)%s\n",
                                        name.c_str(), pairs_bonus_movie.frame_count,
                                        pairs_bonus_movie.parts.size(),
                                        pairs_bonus_movie_ok ? "" : " FAILED");
                        }
                    }
                }
            }
        }
    }
    for (const pple::ConcentrationSlot& slot : pairs_board.slots()) {
        const pple::CardDef* def = pairs_pools.find(slot.card_id);
        CardCache entry;
        entry.index = slot.index;
        entry.base_x = pairs_origin_x + static_cast<float>(slot.x);
        entry.base_y = pairs_origin_y + static_cast<float>(slot.y);
        entry.cell_w = static_cast<float>(slot.w);
        entry.cell_h = static_cast<float>(slot.h);
        float fw = 0.0f, fh = 0.0f;
        if (def == nullptr ||
            !build_card_sprite(def->path, &entry.tex_face, entry.src_face, fw, fh)) {
            ++failed;
        }
        entry.w = entry.src_face.w * card_scale;
        entry.h = entry.src_face.h * card_scale;
        float bw = 0.0f, bh = 0.0f;
        if (!build_card_sprite(pairs_back_xml, &entry.tex_back, entry.src_back, bw, bh))
            ++failed;
        if (entry.w <= 0.0f) {
            entry.w = bw * card_scale;
            entry.h = bh * card_scale;
        }
        card_cache.push_back(entry);
    }
    card_cache.push_back(CardCache{});  // the outline art a hovered card draws; see refresh_cards
    card_cache.back().tex_face = outline_tex;
    card_cache.back().src_face = outline_src;
    card_cache.back().w = outline_w * card_scale;
    card_cache.back().h = outline_h * card_scale;
    // Which cell carries the bonus indicator: Board::SelectSneakCard (0x10008B348) sets Card+0x18
    // on one random card, and Card::DoMatcher's `Card+0x18` branch is what plays the bonus movie
    // and pays the extra token.  Reported because it is the only way to aim a scripted click at
    // it: the shipped game shows it as the golden eye (Card::DoSneakIcon).
    for (int i = 0; i < pairs_board.cell_count(); ++i) {
        if (!pairs_board.sneak_card(i)) continue;
        std::printf("  bonus card: cell %d\n", i);
        break;
    }
    // The shown-face bookkeeping starts from the board as it is now, so a scripted
    // --pairs-click run does not animate a flip for cards that were turned before the first
    // frame.
    for (std::size_t i = 0; i < card_face_shown.size(); ++i)
        card_face_shown[i] = pairs_board.face(static_cast<int>(i));
    // Time bar: two segment templates, one background.  Geometry from the engine --
    // TimeBar::InitUI 0x10008517C loads /TimeBarBkg under /TimeBarContainer and then clones
    // /TimeBarGreen and /TimeBarRed CreateTimeSlice (0x10008C640) times, each stacked at
    // local (3, 21 + i); the registration offsets (-7, 0) / (-8, 0) / (-3, -5) shift each one
    // by the amount NodeSprite::UpdateZero subtracts.  CreateTimeSlice also stores the slice
    // index in the node's frame field, so refresh_cards steps the template's frame ramp with the
    // same index.
    timebar = TimeBarDraw{};
    if (pairs_board.timed() && loaded_scene) {
        float cx = 0.0f, cy = 0.0f;
        for (const pple::SceneItem& item : loaded_scene->containers) {
            const std::string& key = item.name.empty() ? item.instance : item.name;
            if (key == "TimeBarContainer") {
                cx = item.x;
                cy = item.y;
            }
        }
        SDL_FRect green_src{}, red_src{};
        float gw = 0.0f, gh = 0.0f, rw = 0.0f, rh = 0.0f;
        int green_frames = 0;
        timebar.ok =
            build_card_sprite("UI\\Concentration\\GreenTimerBar.xml", &timebar.green, green_src,
                              gw, gh, &green_frames, nullptr, nullptr, &timebar.green_geom) &&
            build_card_sprite("UI\\Concentration\\RedTimerBar.xml", &timebar.red, red_src, rw, rh,
                              nullptr, nullptr, nullptr, &timebar.red_geom);
        if (timebar.ok) {
            // The slices are children of /TimeBarBkg, so their world position is the bkg's own
            // node position (container + 3,21 = 63,4) plus the slice's local (3,21) = (66,25);
            // NodeSprite::UpdateZero then subtracts the slice's registration.  The bkg's own
            // registration (-3,-5) moves only the bkg's drawn corner, not its children, so it
            // must not appear here.
            timebar.x_green = cx + 13.0f;  // container + bkg(3) + slice(3) + green reg 7
            timebar.x_red = cx + 14.0f;    // the same, + red reg 8
            timebar.y_top = cy + 42.0f;    // container + bkg(21) + slice(21), reg_y 0
            // TimeBar::InitUI stores the green template's <FrameCount> as the segment count
            // (0x100085251), and the shipped GreenTimerBar.xml says 226.
            timebar.segments = green_frames;
        }
    }
    return failed;
}

} // namespace pple::viewer
