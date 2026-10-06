#include "app.hpp"

namespace pple::viewer {

TTF_Font* App::font_for(float size) {
    const int key = static_cast<int>(size + 0.5f);
    const auto it = font_cache.find(key);
    if (it != font_cache.end()) return it->second;
    TTF_Font* f = TTF_OpenFont(font_path.c_str(), static_cast<float>(std::max(6, key)));
    font_cache[key] = f;
    return f;
}

void App::rebuild_hud() {
    for (SDL_Texture* t : hud_textures) SDL_DestroyTexture(t);
    hud_textures.clear();
    hud_w = 0;
    hud_h = 0;
    if (font == nullptr) return;
    for (const std::string& line : hud_lines) {
        SDL_Surface* surface =                TTF_RenderText_Blended(font, line.c_str(), line.size(), SDL_Color{
            235, 235, 245, 255
        })
        ;
        if (surface == nullptr) continue;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, surface);
        if (t != nullptr) {
            hud_textures.push_back(t);
            hud_w = std::max(hud_w, surface->w);
            hud_h += surface->h;
        }
        SDL_DestroySurface(surface);
    }
}

void App::refresh_hud() {
    if (!pairs_ready) return;
    const int level_no = pairs_board.level_position() + 1;
    const int level_max = pairs_board.level_count();
    hud_lines = {
        pairs_title,
        "score " + std::to_string(pairs_board.score()) + "   matches " +
            std::to_string(pairs_board.matches()) + "/" +
            std::to_string(pairs_board.round_pairs_total()) +
            (level_max > 1 ? "   level " + std::to_string(level_no) + "/" +
                                 std::to_string(level_max)
                           : std::string()) +
            "   tokens " + std::to_string(pairs_board.tokens()),
        pairs_board.timed() ? "time " + std::to_string(static_cast<int>(pairs_board.seconds_remaining())) + " s"
                            : std::string("untimed"),
        pairs_board.round_won()
            ? "WON - N or R for a new game"
            : (pairs_board.round_over()
                   ? "TIME UP - N or R for a new game"
                   : (pairs_board.won() ? "board complete - the jar is filling"
                                        : "click a card to flip it; N/R new game, ESC menu"))};
    rebuild_hud();
}

} // namespace pple::viewer
