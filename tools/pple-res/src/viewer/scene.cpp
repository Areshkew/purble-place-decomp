#include "app.hpp"

namespace pple::viewer {

// Draw-list teardown.  Sprite and sheet textures belong to sheet_cache and must survive a
// rebuild; only the directly created ones (labels) are freed here.
void App::release_draws() {
    for (const DrawItem& d : scene_draws) {
        if (d.texture == nullptr) continue;
        bool cached = false;
        for (const auto& entry : sheet_cache)
            if (entry.second == d.texture) cached = true;
        if (!cached) SDL_DestroyTexture(d.texture);
    }
    scene_draws.clear();
}



// Builds the scene draw list: every described instance resolved to a sheet frame,
// plus labels rendered through TTF.
void App::build_scene() {
    release_draws();
    scene_button_at.clear();
    scene_w = scene_h = 0.0f;
    if (!scene_background_xml.empty()) {
        const Sprite back = resolve_sprite(bundle, scene_background_xml, 0);
        std::string back_how;
        const auto* back_slot = back.ok ? bundle.resolve(back.requested, back_how) : nullptr;
        if (back_slot != nullptr) {
            std::string note;
            if (SDL_Texture* tex =
                    load_sheet_texture(back.requested, back_slot->entry->name, note)) {
                DrawItem d;
                d.texture = tex;
                float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
                pple::SceneItem geometry;
                geometry.src_x = back.src.x;
                geometry.src_y = back.src.y;
                geometry.src_w = back.src.w;
                geometry.src_h = back.src.h;
                geometry.frames = back.frames;
                geometry.per_row = back.per_row;
                geometry.prepped = back.prepped;
                pple::sprite_frame_rect(geometry, 0, fx, fy, fw, fh);
                d.src = SDL_FRect{fx, fy, fw, fh};
                d.dst = SDL_FRect{0.0f, 0.0f, fw, fh};
                d.alpha = 255;
                scene_draws.push_back(d);
                scene_w = std::max(scene_w, fw);
                scene_h = std::max(scene_h, fh);
            }
        }
    }
    std::string how;
    const auto* slot = bundle.resolve(scene_xml, how);
    if (slot == nullptr) {
        std::fprintf(stderr, "scene not found: %s\n", scene_xml.c_str());
        hud_lines = {
            scene_xml, "scene resource not found"
        };
        rebuild_hud();
        return;
    }
    const std::vector<std::uint8_t> data = bundle.read(*slot);
    const auto fetch = [&](const std::string& name, std::vector<std::uint8_t>& out) {
        std::string inner_how;
        const auto* s = bundle.resolve(name, inner_how);
        if (s == nullptr) return false;
        out = bundle.read(*s);
        return true;
    };
    pple::Scene scene;
    std::string error;
    if (!pple::scene_load(data, fetch, scene, error)) {
        std::fprintf(stderr, "scene parse: %s\n", error.c_str());
        hud_lines = {
            scene_xml, "parse failed: " + error
        };
        rebuild_hud();
        return;
    }
    if (container_offsets) pple::apply_container_offsets(scene);
    loaded_scene = scene;
    std::size_t sprites = 0, labels = 0, movies = 0, drawn_movies = 0, unresolved = 0, empty_frames = 0;
    std::vector<std::string> empty_examples;
    std::vector<std::string> movie_notes;
    std::vector<float> frame_coverage;
    for (const pple::SceneItem& item : scene.items) {
        // ConcentrationGame::LoadGameUI (0x10008122B) deletes these five nodes in this build
        // (g_bMediaCenter == 0).  --mce keeps them and drops them 51 px, which is the only
        // place the document's y = -51 / -41 can land on screen.
        const std::string& mce_key = item.name.empty() ? item.instance : item.name;
        if (hidden_instances.count(mce_key) != 0) continue;
        const bool mce_node = mce_key == "NewGame" || mce_key == "ExitGame" ||
                              mce_key == "NewGameLabel" || mce_key == "ExitGameLabel" ||
                              mce_key == "MatchCountLabel";
        if (mce_node && !mce_mode) continue;
        const float mce_dy = (mce_node && mce_mode) ? 51.0f : 0.0f;
        if (item.kind == pple::NodeKind::Movie) {
            ++movies;
            std::string movie_slot_how;
            const auto* movie_slot = bundle.resolve(item.ref, movie_slot_how);
            if (movie_slot == nullptr) {
                ++unresolved;
                movie_notes.push_back("  " + item.instance + ": " + item.ref + " not found");
                continue;
            }
            pple::Movie movie;
            std::string movie_error;
            if (!pple::movie_load(bundle.read(*movie_slot), fetch, movie, movie_error)) {
                ++unresolved;
                movie_notes.push_back("  " + item.instance + ": " + movie_error);
                continue;
            }
            char summary[224];
            std::snprintf(summary, sizeof summary, "  %-24s -> %-44s %3d frames @%.0f fps, %3zu parts",                              item.instance.c_str(), item.ref.c_str(), movie.frame_count,                              static_cast<double>(pple::movie_frame_rate(movie, movie.parts.front())),                              movie.parts.size());
            const std::size_t used = std::strlen(summary);
            const bool force = !movie_name.empty() && item.instance == movie_name;
            if (item.has_position || force) {
                // A movie the document places (or one named by --movie-name, drawn at
                // --anchor): evaluate frame 0 and lay the parts out.
                const float base_x = item.has_position ? item.x : anchor_x;
                const float base_y = item.has_position ? item.y : anchor_y;
                std::vector<pple::MoviePlacement> placements;
                pple::movie_evaluate(movie, 0, placements);
                std::size_t parts_drawn = 0;
                for (const pple::MoviePlacement& p : placements) {
                    if (p.part == nullptr || !p.part->resolved || p.part->sprite.sheet.empty()) continue;
                    std::string sheet_how;
                    const auto* sheet_slot = bundle.resolve(p.part->sprite.sheet, sheet_how);
                    if (sheet_slot == nullptr) continue;
                    std::string note;
                    SDL_Texture* tex = load_sheet_texture(p.part->sprite.sheet, sheet_slot->entry->name, note);
                    if (tex == nullptr) continue;
                    float sx = 0.0f, sy = 0.0f, sw = 0.0f, sh = 0.0f;
                    pple::sprite_frame_rect(p.part->sprite, 0, sx, sy, sw, sh);
                    DrawItem d;
                    d.texture = tex;
                    d.src = SDL_FRect{
                        sx, sy, sw, sh
                    };
                    d.dst = SDL_FRect{
                        (base_x + p.x - p.part->sprite.reg_x) * item.scale,                                          (base_y + p.y - p.part->sprite.reg_y) * item.scale,                                          sw * p.xscale * item.scale, sh * p.yscale * item.scale
                    };
                    d.alpha = static_cast<int>(std::clamp(p.alpha * 255.0f, 0.0f, 255.0f));
                    d.rotation = p.rotation;
                    scene_draws.push_back(d);
                    scene_w = std::max(scene_w, d.dst.x + d.dst.w);
                    scene_h = std::max(scene_h, d.dst.y + d.dst.h);
                    ++parts_drawn;
                }
                ++drawn_movies;
                std::snprintf(summary + used, sizeof summary - used, "  at %.0f,%.0f -> %zu drawn",                                  static_cast<double>(base_x), static_cast<double>(base_y), parts_drawn);
            } else
            {
                std::snprintf(summary + used, sizeof summary - used,                                  "  (animation slot, no position - render with --movie)");
            }
            movie_notes.push_back(summary);
            continue;
        }
        if (item.kind == pple::NodeKind::Label) {
            ++labels;
            if (item.text.empty()) continue;
            TTF_Font* f = font_for(item.font_size > 0 ? item.font_size : 16.0f);
            if (f == nullptr) continue;
            SDL_Surface* surface =                    TTF_RenderText_Blended(f, item.text.c_str(), item.text.size(),                                           SDL_Color{
                static_cast<Uint8>((item.colour >> 16) & 0xFF),                                                     static_cast<Uint8>((item.colour >> 8) & 0xFF),                                                     static_cast<Uint8>(item.colour & 0xFF),                                                     static_cast<Uint8>((item.colour >> 24) & 0xFF)
            })
            ;
            if (surface == nullptr) continue;
            DrawItem d;
            d.texture = SDL_CreateTextureFromSurface(renderer, surface);
            // <Justify> is a DrawText flag mask, converted in
            // TextRenderer::GetTextRenderInfo (0x1000A02C0):
            //   0x40 -> DT_CENTER, 0x04 -> DT_RIGHT, 0x01 -> DT_BOTTOM|DT_SINGLELINE,
            //   0x20 -> DT_VCENTER|DT_SINGLELINE, 0x10 -> DT_SINGLELINE,
            //   0x80 -> DT_WORDBREAK, 0x02 unused.
            // The shipped values are 64 (centre), 96 (centre + vcentre) and 130 (wrap).
            const float box_w = item.w > 0 ? item.w * item.scale : 0.0f;
            const float box_h = item.h > 0 ? item.h * item.scale : 0.0f;
            float dx = item.x;
            if ((item.justify & 0x04) != 0) {
                if (box_w > 0.0f) dx += box_w - static_cast<float>(surface->w);
            } else if ((item.justify & 0x40) != 0) {
                if (box_w > 0.0f) dx += (box_w - static_cast<float>(surface->w)) * 0.5f;
            }
            float dy = item.y + mce_dy;
            if ((item.justify & 0x01) != 0) {
                if (box_h > 0.0f) dy += box_h - static_cast<float>(surface->h);
            } else if ((item.justify & 0x20) != 0) {
                if (box_h > 0.0f) dy += (box_h - static_cast<float>(surface->h)) * 0.5f;
            }
            d.dst = SDL_FRect{dx, dy, static_cast<float>(surface->w),
                              static_cast<float>(surface->h)};
            d.alpha = item.alpha;
            SDL_DestroySurface(surface);
            scene_draws.push_back(d);
            continue;
        }
        if (item.kind != pple::NodeKind::Sprite && item.kind != pple::NodeKind::Button) {
            ++unresolved;
            continue;
        }
        if (item.sheet.empty()) {
            ++unresolved;
            continue;
        }
        std::string sheet_how;
        const auto* sheet_slot = bundle.resolve(item.sheet, sheet_how);
        if (sheet_slot == nullptr) {
            ++unresolved;
            continue;
        }
        std::string note;
        SDL_Texture* tex = load_sheet_texture(item.sheet, sheet_slot->entry->name, note);
        if (tex == nullptr) {
            ++unresolved;
            continue;
        }
        ++sprites;
        DrawItem d;
        d.texture = tex;
        // Sheets a node marks <PreppedForFilter> sit one pixel inside their slot.
        d.src = SDL_FRect{
            item.src_x, item.src_y, item.src_w, item.src_h
        };
        if (item.prepped) {
            d.src.x += 1.0f;
            d.src.y += 1.0f;
        }
        const float coverage = rect_coverage(sheet_slot->entry->name, d.src);
        if (coverage >= 0.0f && coverage < 25.0f) {
            ++empty_frames;
            if (empty_examples.size() < 6) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "%s(%.0f%%)",                                  (item.name.empty() ? item.instance : item.name).c_str(),                                  static_cast<double>(coverage));
                empty_examples.push_back(buf);
            }
        }
        frame_coverage.push_back(coverage);
        // NodeSprite::UpdateZero (0x100093740): the node sits at its own (x, y) and its size
        // is scale * SourceWidth; the registration offset is subtracted and is itself scaled.
        // A node's own <scale> does not move the node and is not inherited by children.
        d.dst = SDL_FRect{
            item.x - item.reg_x * item.scale, item.y - item.reg_y * item.scale + mce_dy,
            item.src_w * item.scale, item.src_h * item.scale
        };
        d.alpha = item.alpha;
        scene_draws.push_back(d);
        // The jar's tray coins go in front of /JarBkg and behind /JarFg, which is the order
        // Jar::InitUI (0x100083218) builds them in.
        {
            const std::string& key = item.name.empty() ? item.instance : item.name;
            if (key == "JarBase") pairs_jar_base_at = static_cast<int>(scene_draws.size()) - 1;
            if (key == "JarFg") pairs_jar_front_at = static_cast<int>(scene_draws.size()) - 1;
            // A <NodeButton> of the document, remembered by name so a hover can put its second
            // <Animation> in this exact slot (NodeButton::HandleEvent 0x1000AAC92).
            if (item.kind == pple::NodeKind::Button)
                scene_button_at[key] = static_cast<int>(scene_draws.size()) - 1;
        }
        scene_w = std::max(scene_w, d.dst.x + d.dst.w);
        scene_h = std::max(scene_h, d.dst.y + d.dst.h);
    }
    std::printf("scene %s: %zu items -> %zu sprites, %zu labels, %zu movies (%zu drawn), %zu unresolved\n",                    scene_xml.c_str(), scene.items.size(), sprites, labels, movies, drawn_movies, unresolved);
    for (const std::string& note : movie_notes) std::printf("%s\n", note.c_str());
    if (empty_frames != 0) {
        std::printf("  %zu of %zu sprite frames declare a sheet rect with no art in it:",                        empty_frames, sprites);
        for (const std::string& e : empty_examples) std::printf(" %s", e.c_str());
        std::printf("\n");
    }
    for (const std::string& problem : scene.problems) std::printf("  problem: %s\n", problem.c_str());
    std::size_t sprite_index = 0;
    for (const pple::SceneItem& item : scene.items) {
        if (item.kind == pple::NodeKind::Movie) continue;
        std::printf("  %-9s %-22s %-30s pos %.0f,%.0f", pple::node_kind_name(item.kind),                        item.instance.c_str(), item.name.c_str(), static_cast<double>(item.x),                        static_cast<double>(item.y));
        if (!item.sheet.empty()) {
            std::printf("  sheet %s  rect %.0f,%.0f %.0fx%.0f", item.sheet.c_str(),                            static_cast<double>(item.src_x), static_cast<double>(item.src_y),                            static_cast<double>(item.src_w), static_cast<double>(item.src_h));
            if (sprite_index < frame_coverage.size() && frame_coverage[sprite_index] >= 0.0f)                    std::printf("  art %.0f%%", static_cast<double>(frame_coverage[sprite_index]));
            ++sprite_index;
        }
        if (!item.font_name.empty()) std::printf("  font [%s]", item.font_name.c_str());
        std::printf("  text [%s] %.0fpt", item.text.c_str(), static_cast<double>(item.font_size));
        std::printf("\n");
    }
    hud_lines = {
        scene_xml,                     std::to_string(sprites) + " sprites, " + std::to_string(labels) + " labels, " +                         std::to_string(movies) + " movies (" + std::to_string(drawn_movies) + " drawn), " +                         std::to_string(unresolved) + " unresolved" +                         (empty_frames != 0 ? ", " + std::to_string(empty_frames) + " empty frames" : ""),                     "scene extent " + std::to_string(static_cast<int>(scene_w)) + "x" +                         std::to_string(static_cast<int>(scene_h))
    };
    rebuild_hud();
}

// One movie's parts appended to `out` at an anchor.  PurblePlaceMovie::InitFromXml
// (0x100087748) creates every <ImageDef> part as a child of the node it is handed, so a part
// draws at (anchor + part) - registration.


// A label through SDL_ttf, positioned with the <Justify> mask (a DrawText flag mask; see
// section 15 of the notes).  Returns false when it could not be drawn.
//
// A shipped label may carry a line break, spelled as a literal "\n" inside the XML text
// (ColorCountTemplate's "Right Color\nRight Feature", for one); DrawText breaks on it, SDL_ttf
// does not, so the text is split and the lines are stacked.
bool App::append_label(const pple::SceneItem& item) {
    if (item.text.empty()) return false;
    std::vector<std::string> lines;
    std::string current;
    for (std::size_t i = 0; i < item.text.size(); ++i) {
        if (item.text[i] == '\n' ||
            (item.text[i] == '\\' && i + 1 < item.text.size() && item.text[i + 1] == 'n')) {
            lines.push_back(current);
            current.clear();
            if (item.text[i] == '\\') ++i;
            continue;
        }
        current.push_back(item.text[i]);
    }
    lines.push_back(current);
    TTF_Font* f = font_for(item.font_size > 0 ? item.font_size : 16.0f);
    if (f == nullptr) return false;
    const float box_w = item.w > 0 ? item.w * item.scale : 0.0f;
    const float box_h = item.h > 0 ? item.h * item.scale : 0.0f;
    float line_y = item.y * item.scale;
    for (const std::string& line : lines) {
        if (line.empty()) {
            line_y += static_cast<float>(TTF_GetFontHeight(f));
            continue;
        }
        SDL_Surface* surface = TTF_RenderText_Blended(
            f, line.c_str(), line.size(),
            SDL_Color{static_cast<Uint8>((item.colour >> 16) & 0xFF),
                      static_cast<Uint8>((item.colour >> 8) & 0xFF),
                      static_cast<Uint8>(item.colour & 0xFF),
                      static_cast<Uint8>((item.colour >> 24) & 0xFF)});
        if (surface == nullptr) return false;
        float dx = item.x * item.scale;
        if ((item.justify & 0x04) != 0) {
            if (box_w > 0.0f) dx += box_w - static_cast<float>(surface->w);
        } else if ((item.justify & 0x40) != 0) {
            if (box_w > 0.0f) dx += (box_w - static_cast<float>(surface->w)) * 0.5f;
        }
        float dy = line_y;
        // The vertical part of the mask only means anything for a single line: it centres or
        // bottoms that line in the label's own box, which is what the tip and panel labels use.
        if (lines.size() == 1) {
            if ((item.justify & 0x01) != 0) {
                if (box_h > 0.0f) dy += box_h - static_cast<float>(surface->h);
            } else if ((item.justify & 0x20) != 0) {
                if (box_h > 0.0f) dy += (box_h - static_cast<float>(surface->h)) * 0.5f;
            }
        }
        DrawItem d;
        d.texture = SDL_CreateTextureFromSurface(renderer, surface);
        d.dst = SDL_FRect{dx, dy, static_cast<float>(surface->w),
                          static_cast<float>(surface->h)};
        const int line_h = surface->h;
        d.alpha = item.alpha;
        SDL_DestroySurface(surface);
        if (d.texture != nullptr) scene_draws.push_back(d);
        line_y += static_cast<float>(line_h);
    }
    (void)box_h;
    return true;
}

} // namespace pple::viewer
