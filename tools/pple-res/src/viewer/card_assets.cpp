#include "app.hpp"

namespace pple::viewer {

int App::build_pair_card_assets() {
    const float card_scale = static_cast<float>(pairs_board.slots().front().scale_percent) / 100.0f;
    int failed = 0;
    card_cache.clear();
    const auto build_card_sprite = [&](const std::string& xml, SDL_Texture** tex_out,
                                       SDL_FRect& src_out, float& fw_out, float& fh_out,
                                       int* frames_out = nullptr, float* reg_x_out = nullptr,
                                       float* reg_y_out = nullptr) {
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
    // Card+0xB0 (the BlankCard a matched cell shows) and Card+0x110's sparks.
    build_card_sprite(pairs_blank_xml, &pairs_blank_tex, pairs_blank_src, pairs_blank_w,
                      pairs_blank_h, nullptr, &pairs_blank_reg_x, &pairs_blank_reg_y);
    build_card_sprite(pairs_spark_xml, &pairs_spark_tex, pairs_spark_src, pairs_spark_w,
                      pairs_spark_h, nullptr, &pairs_spark_reg_x, &pairs_spark_reg_y);
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
    card_cache.push_back(CardCache{});  // slot for the highlight, filled in by refresh_cards
    card_cache.back().tex_face = outline_tex;
    card_cache.back().src_face = outline_src;
    card_cache.back().w = outline_w * card_scale;
    card_cache.back().h = outline_h * card_scale;
    // The shown-face bookkeeping starts from the board as it is now, so a scripted
    // --pairs-click run does not animate a flip for cards that were turned before the first
    // frame.
    for (std::size_t i = 0; i < card_face_shown.size(); ++i)
        card_face_shown[i] = pairs_board.face(static_cast<int>(i));
    // Time bar: two segment templates, one background.  Geometry from the engine --
    // TimeBar::InitUI 0x10008517C loads /TimeBarBkg under /TimeBarContainer and then clones
    // /TimeBarGreen and /TimeBarRed CreateTimeSlice (0x10008C640) times, each stacked at
    // local (3, 21 + i); the registration offsets (-7, 0) / (-8, 0) / (-3, -5) shift each one
    // by the amount NodeSprite::UpdateZero subtracts.
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
        float gw = 0.0f, gh = 0.0f, rw = 0.0f, rh = 0.0f;
        int green_frames = 0;
        timebar.ok =
            build_card_sprite("UI\\Concentration\\GreenTimerBar.xml", &timebar.green,
                              timebar.green_src, gw, gh, &green_frames) &&
            build_card_sprite("UI\\Concentration\\RedTimerBar.xml", &timebar.red,
                              timebar.red_src, rw, rh);
        if (timebar.ok) {
            timebar.x_green = cx + 16.0f;  // container + bkg(3+3) + slice(3+7)
            timebar.x_red = cx + 17.0f;    // container + bkg(3+3) + slice(3+8)
            timebar.y_top = cy + 47.0f;    // container + bkg(21+5) + slice(21+0)
            // TimeBar::InitUI stores the green template's <FrameCount> as the segment count
            // (0x100085251), and the shipped GreenTimerBar.xml says 226.
            timebar.segments = green_frames;
        }
    }
    return failed;
}

} // namespace pple::viewer
