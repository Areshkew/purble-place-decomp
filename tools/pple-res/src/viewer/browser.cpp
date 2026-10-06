#include "app.hpp"

namespace pple::viewer {

void App::load_current(bool announce) {
    if (texture != nullptr) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    if (sprite_xml.empty() && scene_xml.empty() && movie_xml.empty() && pairs_spec.empty()) {
        current = decode_payload(bundle.read(items[index].slot), items[index].kind);
        src_rect = SDL_FRect{
            0, 0, static_cast<float>(current.width), static_cast<float>(current.height)
        };
        const ResourceEntry* entry = items[index].slot.entry;
        hud_lines = {
            entry->name,                         std::string(kind_name(items[index].kind)) + "   " +                             std::to_string(current.width) + "x" + std::to_string(current.height) +                             (current.note.empty() ? "" : "   " + current.note),                         "item " + std::to_string(index + 1) + " of " + std::to_string(items.size())
        };
        if (!current.ok) {
            if (announce) std::printf("[%zu/%zu] %s -- %s\n", index + 1, items.size(), entry->name.c_str(),                                          current.note.c_str());
            rebuild_hud();
            return;
        }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,                                        current.width, current.height);
        if (texture == nullptr) {
            std::fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
            current.ok = false;
            return;
        }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(texture, nullptr, current.rgba.data(), current.width * 4);
        if (announce)                std::printf("[%zu/%zu] %s  %dx%d  %s  (%s)\n", index + 1, items.size(), entry->name.c_str(),                            current.width, current.height, current.note.c_str(), kind_name(items[index].kind));
        rebuild_hud();
        return;
    }
    if (!scene_xml.empty()) {
        if (pairs_spec.empty())
            build_scene();
        else
            build_pairs();
        return;
    }
    if (!movie_xml.empty()) {
        build_movie();
        return;
    }
    // Sprite mode: resolve the definition, load the sheet, crop one frame.
    const Sprite sprite = resolve_sprite(bundle, sprite_xml, state);
    std::string sheet_note;
    if (!sprite.ok) {
        std::fprintf(stderr, "sprite: %s\n", sprite.note.c_str());
        hud_lines = {
            sprite_xml, sprite.note
        };
        rebuild_hud();
        return;
    }
    texture = load_sheet_texture(sprite.requested, sprite.resolved, sheet_note);
    if (texture == nullptr) {
        std::fprintf(stderr, "sheet: %s\n", sheet_note.c_str());
        hud_lines = {
            sprite_xml, sprite.note, sheet_note
        };
        rebuild_hud();
        return;
    }
    {
        pple::SceneItem geometry;
        geometry.src_x = sprite.src.x;
        geometry.src_y = sprite.src.y;
        geometry.src_w = sprite.src.w;
        geometry.src_h = sprite.src.h;
        geometry.frames = sprite.frames;
        geometry.per_row = sprite.per_row;
        geometry.prepped = sprite.prepped;
        pple::sprite_frame_rect(geometry, static_cast<int>(frame), src_rect.x, src_rect.y, src_rect.w,                                    src_rect.h);
    }
    current.width = static_cast<int>(src_rect.w);
    current.height = static_cast<int>(src_rect.h);
    current.ok = true;
    hud_lines = {
        sprite_xml,                     "Texture " + sprite.requested + "   (" + sprite.how + ")",                     "sheet   " + sprite.resolved + "   " + sheet_note,                     "frame   " + std::to_string(frame) + " of " + std::to_string(sprite.frames) + "   rect " +                         std::to_string(static_cast<int>(src_rect.x)) + "," +                         std::to_string(static_cast<int>(src_rect.y)) + " " +                         std::to_string(static_cast<int>(src_rect.w)) + "x" +                         std::to_string(static_cast<int>(src_rect.h)) + "   SheetClass " + sprite.sheet_class
    };
    std::printf("[sprite] %s -> %s (%s)\n  sheet %s %s\n  frame %zu rect %.0f,%.0f %.0fx%.0f\n",                    sprite.requested.c_str(), sprite.resolved.c_str(), sprite.how.c_str(), sprite.resolved.c_str(),                    sheet_note.c_str(), frame, static_cast<double>(src_rect.x), static_cast<double>(src_rect.y),                    static_cast<double>(src_rect.w), static_cast<double>(src_rect.h));
    for (const std::string& step : named_steps) std::printf("  %s\n", step.c_str());
    rebuild_hud();
}

} // namespace pple::viewer
