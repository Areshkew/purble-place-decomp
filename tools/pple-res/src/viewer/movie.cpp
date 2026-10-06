#include "app.hpp"

namespace pple::viewer {

void App::build_movie() {
    release_draws();
    scene_w = scene_h = 0.0f;
    std::string how;
    const auto* slot = bundle.resolve(movie_xml, how);
    if (slot == nullptr) {
        std::fprintf(stderr, "movie not found: %s\n", movie_xml.c_str());
        hud_lines = {
            movie_xml, "movie resource not found"
        };
        rebuild_hud();
        return;
    }
    const auto fetch = [&](const std::string& name, std::vector<std::uint8_t>& out) {
        std::string inner_how;
        const auto* s = bundle.resolve(name, inner_how);
        if (s == nullptr) return false;
        out = bundle.read(*s);
        return true;
    };
    pple::Movie movie;
    std::string error;
    if (!pple::movie_load(bundle.read(*slot), fetch, movie, error)) {
        std::fprintf(stderr, "movie parse: %s\n", error.c_str());
        hud_lines = {
            movie_xml, "parse failed: " + error
        };
        rebuild_hud();
        return;
    }
    std::vector<pple::MoviePlacement> placements;
    pple::movie_evaluate(movie, static_cast<int>(frame), placements);
    std::size_t drawn = 0, missing = 0;
    for (const pple::MoviePlacement& p : placements) {
        if (p.part == nullptr || !p.part->resolved || p.part->sprite.sheet.empty()) {
            ++missing;
            continue;
        }
        std::string sheet_how;
        const auto* sheet_slot = bundle.resolve(p.part->sprite.sheet, sheet_how);
        if (sheet_slot == nullptr) {
            ++missing;
            continue;
        }
        std::string note;
        SDL_Texture* tex = load_sheet_texture(p.part->sprite.sheet, sheet_slot->entry->name, note);
        if (tex == nullptr) {
            ++missing;
            continue;
        }
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        pple::sprite_frame_rect(p.part->sprite, 0, x, y, w, h);
        DrawItem d;
        d.texture = tex;
        d.src = SDL_FRect{
            x, y, w, h
        };
        d.dst = SDL_FRect{
            anchor_x + p.x - p.part->sprite.reg_x, anchor_y + p.y - p.part->sprite.reg_y,                              w * p.xscale, h * p.yscale
        };
        d.alpha = static_cast<int>(std::clamp(p.alpha * 255.0f, 0.0f, 255.0f));
        d.rotation = p.rotation;
        scene_draws.push_back(d);
        scene_w = std::max(scene_w, d.dst.x + d.dst.w);
        scene_h = std::max(scene_h, d.dst.y + d.dst.h);
        ++drawn;
    }
    if (scene_w <= 0.0f) scene_w = static_cast<float>(width);
    if (scene_h <= 0.0f) scene_h = static_cast<float>(height);
    std::printf("movie %s: %d frame(s) at %.0f fps, %zu part(s) -> %zu drawn, %zu unresolved\n",                    movie_xml.c_str(), movie.frame_count,                    static_cast<double>(pple::movie_frame_rate(movie, movie.parts.front())), movie.parts.size(),                    drawn, missing);
    std::printf("  frame %zu of %d, anchor %.0f,%.0f\n", frame, movie.frame_count - 1,                    static_cast<double>(anchor_x), static_cast<double>(anchor_y));
    for (const std::string& problem : movie.problems) std::printf("  problem: %s\n", problem.c_str());
    hud_lines = {
        movie_xml,                     std::to_string(movie.frame_count) + " frames, " + std::to_string(movie.parts.size()) +                         " parts, " + std::to_string(drawn) + " drawn",                     "frame " + std::to_string(frame) + (movie.problems.empty() ? "" : ", problems")
    };
    rebuild_hud();
}

int App::append_movie(const pple::Movie& mv, int movie_frame, float ax, float ay,
                                  std::vector<DrawItem>& out, float& extent_w,
                                  float& extent_h) {
    if (mv.parts.empty()) return 0;
    std::vector<pple::MoviePlacement> placements;
    pple::movie_evaluate(mv, movie_frame, placements);
    int drawn = 0;
    for (const pple::MoviePlacement& p : placements) {
        if (p.part == nullptr || !p.part->resolved || p.part->sprite.sheet.empty()) continue;
        std::string sheet_how;
        const auto* sheet_slot = bundle.resolve(p.part->sprite.sheet, sheet_how);
        if (sheet_slot == nullptr) continue;
        std::string note;
        SDL_Texture* tex =
            load_sheet_texture(p.part->sprite.sheet, sheet_slot->entry->name, note);
        if (tex == nullptr) continue;
        float sx = 0.0f, sy = 0.0f, sw = 0.0f, sh = 0.0f;
        pple::sprite_frame_rect(p.part->sprite, 0, sx, sy, sw, sh);
        DrawItem d;
        d.texture = tex;
        d.src = SDL_FRect{sx, sy, sw, sh};
        d.dst = SDL_FRect{ax + p.x - p.part->sprite.reg_x, ay + p.y - p.part->sprite.reg_y,
                          sw * p.xscale, sh * p.yscale};
        d.alpha = static_cast<int>(std::clamp(p.alpha * 255.0f, 0.0f, 255.0f));
        d.rotation = p.rotation;
        out.push_back(d);
        extent_w = std::max(extent_w, d.dst.x + d.dst.w);
        extent_h = std::max(extent_h, d.dst.y + d.dst.h);
        ++drawn;
    }
    return drawn;
}

} // namespace pple::viewer
