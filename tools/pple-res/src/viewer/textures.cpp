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

namespace {
// The engine's double-DPI name for an image resource: ResourceTextureD3D::SetNameForDPI
// (0x1000A80DC) inserts "x2" before the extension, so UI\PurblePlace\Loading.jpg asks for
// UI\PurblePlace\Loadingx2.jpg and Sheets\concentrationSheet29.sht asks for
// Sheets\concentrationSheet29x2.sht, which resolves to ...Sheet29x2.jpg / .bmp.cab.
std::string dpi_sibling(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) return name + "x2";
    return name.substr(0, dot) + "x2" + name.substr(dot);
}
int pot_up(int v) {
    int p = 1;
    while (p < v) p *= 2;
    return p;
}
}  // namespace

// Fraction of the sheet's occupancy grid that falls inside a declared frame rect.
float App::rect_coverage(const std::string& sheet, const SDL_FRect& src) {
    const auto cov = sheet_coverage.find(sheet);
    const auto size = sheet_sizes.find(sheet);
    if (cov == sheet_coverage.end() || size == sheet_sizes.end()) return -1.0f;
    // `src` is authored in 1x sheet space; the grid describes the texture that was uploaded,
    // which is twice as large when the sheet came from its x2 resources.
    float scale = 1.0f;
    if (const auto s = sheet_src_scale.find(sheet); s != sheet_src_scale.end()) scale = s->second;
    const SDL_FRect box{src.x * scale, src.y * scale, src.w * scale, src.h * scale};
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
            if (cx < box.x || cx >= box.x + box.w || cy < box.y || cy >= box.y + box.h) continue;
            ++total;
            filled += cov->second[static_cast<std::size_t>(gy) * 64 + gx];
        }
    }
    return total == 0 ? -1.0f : 100.0f * static_cast<float>(filled) / static_cast<float>(total);
}

// SplitList.xml, read once: it names the sheets whose pixels are shipped as four x2 pages.
bool App::load_split_list() {
    if (split_sheets_loaded) return !split_sheets.empty();
    split_sheets_loaded = true;
    const auto fetch = [this](const std::string& name, std::vector<std::uint8_t>& out) {
        return read_resource(name, out);
    };
    std::string error;
    if (!pple::split_list_load(fetch, split_sheets, error)) {
        std::printf("hidpi: SplitList.xml did not load (%s)\n", error.c_str());
        split_sheets.clear();
        return false;
    }
    return true;
}

// The surface a sheet request turns into at the current DPI.  In double-DPI mode the engine
// reads the x2 resources instead of the base ones (ResourceTextureD3D::Recreate 0x1000A7FEC):
// a sheet SplitList.xml lists is its four `<stem>x2<page>` pages, one per UV quadrant holding
// that quadrant's pixels at 2x (Material::Render 0x1000A96E0), and every other image is its
// "x2"-suffixed sibling.  A missing x2 resource falls back to the 1x art and is counted.
SDL_Surface* App::load_sheet_surface(const std::string& requested, const std::string& resolved_name,
                                     float& src_scale, bool& composed, std::string& note) {
    src_scale = 1.0f;
    composed = false;
    named_steps.clear();
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
        // Everything that is not a CAB is one image resource.  The desktop viewer keeps
        // SDL3_image here, byte for byte as before; the web build has no SDL3_image and goes
        // through the shared payload decoder, which uses the project's own PNG decoder and
        // Emscripten's libjpeg.  The two decoders can differ by a unit per channel on a PNG
        // sheet (measured in notes section 50); keeping the desktop path untouched is what
        // stops that difference leaking into every existing capture.
#ifdef PPLE_HAVE_SDL_IMAGE
        SDL_IOStream* io = SDL_IOFromConstMem(payload.data(), payload.size());
        if (io == nullptr) {
            why = std::string("io: ") + SDL_GetError();
            return nullptr;
        }
        return IMG_Load_IO(io, true);
#else
        const Decoded decoded = decode_payload(payload, kind);
        if (!decoded.ok) {
            why = decoded.note.empty() ? std::string("decode failed") : decoded.note;
            return nullptr;
        }
        SDL_Surface* surface = SDL_CreateSurface(decoded.width, decoded.height,
                                                 SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            why = std::string("SDL_CreateSurface: ") + SDL_GetError();
            return nullptr;
        }
        auto* pixels = static_cast<std::uint8_t*>(surface->pixels);
        for (int y = 0; y < decoded.height; ++y) {
            std::memcpy(pixels + static_cast<std::size_t>(y) * surface->pitch,
                        decoded.rgba.data() + static_cast<std::size_t>(y) * decoded.width * 4,
                        static_cast<std::size_t>(decoded.width) * 4);
        }
        return surface;
#endif
    };
    const bool is_sht = requested.size() > 4 &&
                        requested.compare(requested.size() - 4, 4, ".sht") == 0;
    // A .sht name is a colour/alpha pair: the JPEG supplies RGB and the BMP's byte becomes the
    // alpha channel verbatim (ResourceTextureD3D::CreateTextureFromSource 0x1000A8A6D).
    const auto compose_sht = [&](const std::string& sht_name, std::string& why) -> SDL_Surface* {
        SDL_Surface* colour = nullptr;
        SDL_Surface* alpha = nullptr;
        std::string inner_why;
        const std::string jpg = sht_sibling(sht_name, "jpg");
        const std::string bmp = sht_sibling(sht_name, "bmp");
        std::string jpg_how, bmp_how;
        if (const auto* s = bundle.resolve(jpg, jpg_how)) {
            colour = surface_for(s->entry->name, inner_why);
            named_steps.push_back("colour " + s->entry->name + " (" + jpg_how + ")");
        }
        if (const auto* s = bundle.resolve(bmp, bmp_how)) {
            alpha = surface_for(s->entry->name, inner_why);
            named_steps.push_back("alpha  " + s->entry->name + " (" + bmp_how + ")");
        }
        if (colour == nullptr) {
            why = "no colour sheet for " + sht_name;
            if (alpha != nullptr) SDL_DestroySurface(alpha);
            return nullptr;
        }
        SDL_Surface* rgba = SDL_ConvertSurface(colour, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(colour);
        if (rgba == nullptr) {
            why = std::string("convert: ") + SDL_GetError();
            if (alpha != nullptr) SDL_DestroySurface(alpha);
            return nullptr;
        }
        if (alpha != nullptr) {
            composed = true;
            SDL_Surface* amask = SDL_ConvertSurface(alpha, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(alpha);
            if (amask != nullptr) {
                if (amask->w != rgba->w || amask->h != rgba->h) {
                    why = "colour " + std::to_string(rgba->w) + "x" + std::to_string(rgba->h) +
                          " but alpha " + std::to_string(amask->w) + "x" + std::to_string(amask->h) +
                          " (engine logs \"Alpha and color dimensions don't match\")";
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
        return rgba;
    };

    if (sheet_dpi == SheetDpi::Double) {
        load_split_list();
        const pple::SplitSheet* split =
            is_sht ? pple::split_sheet_find(split_sheets, requested) : nullptr;
        if (split != nullptr && split->width > 0 && split->height > 0) {
            // GetSplitTexture (0x1000A7E40) builds `<stem>x2<page>` (page 1..4) and
            // Material::Render (0x1000A96E0) picks it by UV quadrant, so page p holds the
            // quadrant at (col * width, row * height) of a canvas twice the virtual size.
            SDL_Surface* canvas = SDL_CreateSurface(split->width * 2, split->height * 2,
                                                    SDL_PIXELFORMAT_RGBA32);
            if (canvas == nullptr) {
                note = std::string("SDL_CreateSurface: ") + SDL_GetError();
            } else {
                SDL_SetSurfaceBlendMode(canvas, SDL_BLENDMODE_NONE);
                const SDL_Rect all{0, 0, canvas->w, canvas->h};
                SDL_FillSurfaceRect(canvas, &all, 0);
                bool ok = true;
                for (int page = 0; page < 4 && ok; ++page) {
                    const std::string page_name = pple::split_page_name(split->name, page);
                    std::string why;
                    SDL_Surface* piece = compose_sht(page_name, why);
                    if (piece == nullptr) {
                        ok = false;
                        note = "page " + std::to_string(page + 1) + " of " + split->name + ": " + why;
                        break;
                    }
                    SDL_SetSurfaceBlendMode(piece, SDL_BLENDMODE_NONE);
                    const SDL_Rect at{(page % 2) * split->width, (page / 2) * split->height, 0, 0};
                    SDL_BlitSurface(piece, nullptr, canvas, &at);
                    SDL_DestroySurface(piece);
                }
                if (ok) {
                    src_scale = 2.0f;
                    note = "x2 split sheet, 4 pages composed";
                    ++hidpi_sheets;
                    return canvas;
                }
                SDL_DestroySurface(canvas);
            }
        } else {
            const std::string x2 = dpi_sibling(requested);
            std::string how;
            if (const auto* slot = bundle.resolve(x2, how)) {
                std::string why;
                if (is_sht) {
                    if (SDL_Surface* surface = compose_sht(x2, why)) {
                        src_scale = 2.0f;
                        note = "x2 sheet sibling (" + how + ")";
                        ++hidpi_sheets;
                        return surface;
                    }
                    note = "x2 sheet sibling " + x2 + ": " + why;
                } else if (SDL_Surface* colour = surface_for(slot->entry->name, why)) {
                    SDL_Surface* rgba = SDL_ConvertSurface(colour, SDL_PIXELFORMAT_RGBA32);
                    SDL_DestroySurface(colour);
                    if (rgba != nullptr) {
                        src_scale = 2.0f;
                        note = "x2 image " + slot->entry->name;
                        ++hidpi_sheets;
                        return rgba;
                    }
                    note = std::string("convert: ") + SDL_GetError();
                } else {
                    note = "x2 image " + slot->entry->name + ": " + why;
                }
            }
        }
        ++hidpi_fallbacks;
        // A named gap rather than a silent mixture of resolutions: this resource has no x2
        // variant, so it is drawn from the 1x art even in double-DPI mode.
        std::printf("hidpi: no x2 resource for %s, using the 1x art\n", requested.c_str());
        named_steps.push_back("no x2 resource for " + requested + ", using the 1x art");
        src_scale = 1.0f;
    }

    if (is_sht) {
        std::string why;
        SDL_Surface* surface = compose_sht(requested, why);
        if (surface == nullptr) note = why;
        return surface;
    }
    std::string why;
    SDL_Surface* colour = surface_for(resolved_name, why);
    if (colour == nullptr) {
        note = "no colour sheet for " + requested;
        return nullptr;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(colour, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(colour);
    if (rgba == nullptr) note = std::string("convert: ") + SDL_GetError();
    return rgba;
}

SDL_Texture* App::load_sheet_texture(const std::string& requested, const std::string& resolved_name,                                        std::string& note) {
    const auto cached = sheet_cache.find(resolved_name);
    if (cached != sheet_cache.end()) {
        const auto size = sheet_sizes[resolved_name];
        note = std::to_string(size.first) + "x" + std::to_string(size.second) + " (cached)";
        return cached->second;
    }
    float src_scale = 1.0f;
    bool composed = false;
    SDL_Surface* rgba = load_sheet_surface(requested, resolved_name, src_scale, composed, note);
    if (rgba == nullptr) return nullptr;
    // The engine uploads every texture at the next power of two above each dimension and
    // then reports *that* size (ResourceTextureD3D::CreateTextureFromSource 0x1000A8F64:
    // `if ((w & -w) != w) { for (i = 1; i < w; i *= 2); w = i; }` for both axes, then
    // `*a3 = w; *a4 = h;`).  A node's <SheetOffsetX>/<SourceWidth> are in that padded space,
    // not in file space: <PlayArea> asks for 148,0 800x500 out of an 800x496 PNG, which only
    // fits because the texture is 1024x512.  Padding here reproduces that, and the padding
    // is left fully transparent, which is what a freshly created D3D texture holds.
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
    if (src_scale > 1.0f) note += "  x2 double-DPI art";
    sheet_sizes[resolved_name] = {
        rgba->w, rgba->h
    };
    build_coverage(rgba, sheet_coverage[resolved_name], composed);
    SDL_DestroySurface(rgba);
    if (tex != nullptr) {
        sheet_cache[resolved_name] = tex;
        sheet_src_scale[resolved_name] = src_scale;
        texture_src_scale[tex] = src_scale;
    }
    return tex;
}

// The source-rect multiplier for a texture: 2 when it was loaded from the x2 resources.
float App::src_scale_for(SDL_Texture* tex) const {
    const auto it = texture_src_scale.find(tex);
    return it == texture_src_scale.end() ? 1.0f : it->second;
}

// Every cached sheet belongs to one DPI; a switch drops all of them.
void App::clear_sheet_textures() {
    for (const auto& [name, tex] : sheet_cache) SDL_DestroyTexture(tex);
    sheet_cache.clear();
    sheet_sizes.clear();
    sheet_coverage.clear();
    sheet_src_scale.clear();
    texture_src_scale.clear();
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
