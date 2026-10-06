#include "app.hpp"

namespace pple::viewer {

// `alpha_only` is set for sheets composed from a colour/alpha pair: their background is
// genuinely transparent, so the alpha channel alone decides occupancy.  A plain JPEG has
// no alpha and paints its empty area flat black, so both signals are needed there.
void App::build_coverage(SDL_Surface* surface, std::vector<std::uint8_t>& grid,                                   bool alpha_only) {
    grid.assign(64 * 64, 0);
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    if (rgba == nullptr) return;
    for (int gy = 0;
    gy < 64;
    ++gy) {
        for (int gx = 0;
        gx < 64;
        ++gx) {
            const int x = gx * rgba->w / 64;
            const int y = gy * rgba->h / 64;
            const auto* row =                    static_cast<const std::uint8_t*>(rgba->pixels) + static_cast<std::size_t>(y) * rgba->pitch;
            const std::uint8_t r = row[x * 4 + 0], g = row[x * 4 + 1], b = row[x * 4 + 2],                                  a = row[x * 4 + 3];
            grid[static_cast<std::size_t>(gy) * 64 + gx] =                    (a > 8 && (alpha_only || (r + g + b) > 24)) ? 1 : 0;
        }
    }
    SDL_DestroySurface(rgba);
}

// Fraction of the sheet's occupancy grid that falls inside a declared frame rect.
float App::rect_coverage(const std::string& sheet, const SDL_FRect& src) {
    const auto cov = sheet_coverage.find(sheet);
    const auto size = sheet_sizes.find(sheet);
    if (cov == sheet_coverage.end() || size == sheet_sizes.end()) return -1.0f;
    const float cw = static_cast<float>(size->second.first) / 64.0f;
    const float ch = static_cast<float>(size->second.second) / 64.0f;
    int total = 0, filled = 0;
    for (int gy = 0;
    gy < 64;
    ++gy) {
        for (int gx = 0;
        gx < 64;
        ++gx) {
            const float cx = (static_cast<float>(gx) + 0.5f) * cw;
            const float cy = (static_cast<float>(gy) + 0.5f) * ch;
            if (cx < src.x || cx >= src.x + src.w || cy < src.y || cy >= src.y + src.h) continue;
            ++total;
            filled += cov->second[static_cast<std::size_t>(gy) * 64 + gx];
        }
    }
    return total == 0 ? -1.0f : 100.0f * static_cast<float>(filled) / static_cast<float>(total);
}

SDL_Texture* App::load_sheet_texture(const std::string& requested, const std::string& resolved_name,                                        std::string& note) {
    const auto cached = sheet_cache.find(resolved_name);
    if (cached != sheet_cache.end()) {
        const auto size = sheet_sizes[resolved_name];
        note = std::to_string(size.first) + "x" + std::to_string(size.second) + " (cached)";
        return cached->second;
    }
    // Resource name -> surface.  CAB payloads hold an uncompressed BMP.
    const auto surface_for = [&](const std::string& name, std::string& why) -> SDL_Surface* {
        const auto* slot = bundle.find(name);
        if (slot == nullptr) {
            why = "not found";
            return nullptr;
        }
        const std::vector<std::uint8_t> payload = bundle.read(*slot);
        const Kind kind = classify(payload);
        if (kind == Kind::Cab) {
            std::string error;
            const auto cab = pple::CabArchive::parse(payload, error);
            if (!cab) {
                why = "cab: " + error;
                return nullptr;
            }
            std::vector<std::uint8_t> bmp;
            if (!cab->extract_single(payload, bmp, error)) {
                why = "cab: " + error;
                return nullptr;
            }
            SDL_IOStream* io = SDL_IOFromConstMem(bmp.data(), bmp.size());
            if (io == nullptr) {
                why = std::string("io: ") + SDL_GetError();
                return nullptr;
            }
            return SDL_LoadBMP_IO(io, true);
        }
        SDL_IOStream* io = SDL_IOFromConstMem(payload.data(), payload.size());
        if (io == nullptr) {
            why = std::string("io: ") + SDL_GetError();
            return nullptr;
        }
        return IMG_Load_IO(io, true);
    };
    SDL_Surface* colour = nullptr;
    SDL_Surface* alpha = nullptr;
    bool composed = false;
    std::string colour_name = resolved_name, alpha_name;
    if (requested.size() > 4 && requested.compare(requested.size() - 4, 4, ".sht") == 0) {
        named_steps.clear();
        std::string why;
        const std::string jpg = sht_sibling(requested, "jpg");
        const std::string bmp = sht_sibling(requested, "bmp");
        std::string jpg_how, bmp_how;
        if (const auto* s = bundle.resolve(jpg, jpg_how)) {
            colour = surface_for(s->entry->name, why);
            colour_name = s->entry->name;
            named_steps.push_back("colour " + colour_name + " (" + jpg_how + ")");
        }
        if (const auto* s = bundle.resolve(bmp, bmp_how)) {
            alpha = surface_for(s->entry->name, why);
            alpha_name = s->entry->name;
            named_steps.push_back("alpha  " + alpha_name + " (" + bmp_how + ")");
        }
        composed = colour != nullptr && alpha != nullptr;
    } else
    {
        std::string why;
        colour = surface_for(resolved_name, why);
    }
    if (colour == nullptr) {
        note = "no colour sheet for " + requested;
        if (alpha != nullptr) SDL_DestroySurface(alpha);
        return nullptr;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(colour, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(colour);
    if (rgba == nullptr) {
        note = std::string("convert: ") + SDL_GetError();
        if (alpha != nullptr) SDL_DestroySurface(alpha);
        return nullptr;
    }
    if (composed) {
        SDL_Surface* amask = SDL_ConvertSurface(alpha, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(alpha);
        if (amask != nullptr) {
            if (amask->w != rgba->w || amask->h != rgba->h) {
                note = "colour " + std::to_string(rgba->w) + "x" + std::to_string(rgba->h) +                           " but alpha " + std::to_string(amask->w) + "x" + std::to_string(amask->h) +                           " (engine logs \"Alpha and color dimensions don't match\")";
                SDL_DestroySurface(amask);
                SDL_DestroySurface(rgba);
                return nullptr;
            }
            const auto* asrc = static_cast<const std::uint8_t*>(amask->pixels);
            auto* dst = static_cast<std::uint8_t*>(rgba->pixels);
            for (int y = 0;
            y < rgba->h;
            ++y) {
                const auto* arow = asrc + static_cast<std::size_t>(y) * amask->pitch;
                auto* drow = dst + static_cast<std::size_t>(y) * rgba->pitch;
                for (int x = 0;
                x < rgba->w;
                ++x) {
                    // The engine takes the BMP's byte as the alpha channel verbatim.
                    drow[x * 4 + 3] = arow[x * 4];
                }
            }
            SDL_DestroySurface(amask);
        }
    }
    // The engine uploads every texture at the next power of two above each dimension and
    // then reports *that* size (ResourceTextureD3D::CreateTextureFromSource 0x1000A8F64:
    // `if ((w & -w) != w) { for (i = 1; i < w; i *= 2); w = i; }` for both axes, then
    // `*a3 = w; *a4 = h;`).  A node's <SheetOffsetX>/<SourceWidth> are in that padded space,
    // not in file space: <PlayArea> asks for 148,0 800x500 out of an 800x496 PNG, which only
    // fits because the texture is 1024x512.  Padding here reproduces that, and the padding
    // is left fully transparent, which is what a freshly created D3D texture holds.
    auto pot_up = [](int v) {
        int p = 1;
        while (p < v) p *= 2;
        return p;
    };
    const int file_w = rgba->w, file_h = rgba->h;
    const int padded_w = pot_up(file_w);
    const int padded_h = pot_up(file_h);
    if (padded_w != file_w || padded_h != file_h) {
        if (SDL_Surface* padded = SDL_CreateSurface(padded_w, padded_h, SDL_PIXELFORMAT_RGBA32)) {
            SDL_SetSurfaceBlendMode(padded, SDL_BLENDMODE_NONE);
            SDL_SetSurfaceBlendMode(rgba, SDL_BLENDMODE_NONE);
            const SDL_Rect all{0, 0, padded_w, padded_h};
            SDL_FillSurfaceRect(padded, &all, 0);
            SDL_BlitSurface(rgba, nullptr, padded, nullptr);
            SDL_DestroySurface(rgba);
            // Restore the blend mode: SDL_CreateTextureFromSurface inherits it, and a NONE
            // texture would draw the sheet's transparent black as opaque black.
            SDL_SetSurfaceBlendMode(padded, SDL_BLENDMODE_BLEND);
            rgba = padded;
        }
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, rgba);
    if (tex != nullptr) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    note = std::to_string(rgba->w) + "x" + std::to_string(rgba->h) +
           (composed ? "  colour+alpha composed" : "");
    if (padded_w != file_w || padded_h != file_h)
        note += "  (file " + std::to_string(file_w) + "x" + std::to_string(file_h) +
                ", padded to a power of two as the engine does)";
    sheet_sizes[resolved_name] = {
        rgba->w, rgba->h
    };
    build_coverage(rgba, sheet_coverage[resolved_name], composed);
    SDL_DestroySurface(rgba);
    if (tex != nullptr) sheet_cache[resolved_name] = tex;
    return tex;
}

// One shared resource fetch, in the shape pple::scene_load / pple::movie_load want.
bool App::read_resource(const std::string& name, std::vector<std::uint8_t>& out) {
    std::string inner_how;
    const auto* s = bundle.resolve(name, inner_how);
    if (s == nullptr) return false;
    out = bundle.read(*s);
    return true;
}

} // namespace pple::viewer
