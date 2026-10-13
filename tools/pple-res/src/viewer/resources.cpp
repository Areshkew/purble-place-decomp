#include "resources.hpp"

namespace pple::viewer {
// --mui installs a localiser, so "|id|context|English" labels render from the .mui instead of
// from the English text embedded in the data.
pple::MuiStrings* g_mui_for_labels = nullptr;
std::size_t g_mui_hits = 0, g_mui_misses = 0;

std::string mui_resolve(int id, const std::string& fallback, void*) {
    if (g_mui_for_labels == nullptr) return fallback;
    const std::u16string* found = g_mui_for_labels->find(id);
    if (found == nullptr) {
        ++g_mui_misses;
        return fallback;
    }
    ++g_mui_hits;
    return pple::utf8_from_utf16(*found);
}
    std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    std::string normalise(const std::string& s) {
        std::string out = lower(s);
        for (char& c : out)        if (c == '/') c = '\\';
        return out;
    }
    const char* kind_name(Kind k) {
        switch (k) {
            case Kind::Png:
            return "PNG";
            case Kind::Cab:
            return "BMP in CAB";
            case Kind::Jpeg:
            return "JPEG";
            case Kind::Xml:
            return "XML";
            case Kind::Audio:
            return "WMA";
            default:
            return "other";
        }
    }
    Kind classify(std::span<const std::uint8_t> d) {
        if (d.size() >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return Kind::Png;
        if (pple::CabArchive::looks_like_cab(d)) return Kind::Cab;
        if (d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return Kind::Jpeg;
        if (d.size() >= 4 && d[0] == 0x30 && d[1] == 0x26 && d[2] == 0xB2 && d[3] == 0x75) return Kind::Audio;
        if (d.size() >= 5 && (std::memcmp(d.data(), "<?xml", 5) == 0 || d[0] == '<' ||                          (d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF)))        return Kind::Xml;
        return Kind::Other;
    }
    bool surface_to_rgba(SDL_Surface* surface, Decoded& out) {
        SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        if (converted == nullptr) {
            out.note = std::string("SDL_ConvertSurface: ") + SDL_GetError();
            return false;
        }
        out.width = converted->w;
        out.height = converted->h;
        out.rgba.assign(static_cast<std::size_t>(converted->w) * converted->h * 4, 0);
        for (int y = 0;
        y < converted->h;
        ++y) {
            const auto* src = static_cast<const std::uint8_t*>(converted->pixels) +                          static_cast<std::size_t>(y) * converted->pitch;
            std::memcpy(out.rgba.data() + static_cast<std::size_t>(y) * converted->w * 4, src,                    static_cast<std::size_t>(converted->w) * 4);
        }
        SDL_DestroySurface(converted);
        return true;
    }
    Decoded decode_payload(std::span<const std::uint8_t> payload, Kind kind) {
        Decoded out;
        if (payload.empty()) {
            out.note = "empty resource";
            return out;
        }
        if (kind == Kind::Png) {
            pple::PngImage image;
            std::string error;
            if (!pple::png_decode(payload, image, error)) {
                out.note = "png: " + error;
                return out;
            }
            out.rgba = std::move(image.rgba);
            out.width = static_cast<int>(image.width);
            out.height = static_cast<int>(image.height);
            out.note = "depth " + std::to_string(image.bit_depth) + " " + image.colour_type_name();
            out.ok = true;
            return out;
        }
        if (kind == Kind::Cab) {
            std::string error;
            const auto cab = pple::CabArchive::parse(payload, error);
            if (!cab) {
                out.note = "cab: " + error;
                return out;
            }
            std::vector<std::uint8_t> bmp;
            if (!cab->extract_single(payload, bmp, error)) {
                out.note = "cab: " + error;
                return out;
            }
            SDL_IOStream* io = SDL_IOFromConstMem(bmp.data(), bmp.size());
            if (io == nullptr) {
                out.note = std::string("SDL_IOFromConstMem: ") + SDL_GetError();
                return out;
            }
            SDL_Surface* surface = SDL_LoadBMP_IO(io, true);
            if (surface == nullptr) {
                out.note = std::string("SDL_LoadBMP_IO: ") + SDL_GetError();
                return out;
            }
            const bool ok = surface_to_rgba(surface, out);
            SDL_DestroySurface(surface);
            if (!ok) return out;
            out.note = cab->files[0].name + ", " + std::to_string(cab->blocks.size()) + " CFDATA, MSZIP";
            out.ok = true;
            return out;
        }
        if (kind == Kind::Jpeg) {
#ifdef PPLE_HAVE_SDL_IMAGE
            SDL_IOStream* io = SDL_IOFromConstMem(payload.data(), payload.size());
            if (io == nullptr) {
                out.note = std::string("SDL_IOFromConstMem: ") + SDL_GetError();
                return out;
            }
            SDL_Surface* surface = IMG_Load_IO(io, true);
            if (surface == nullptr) {
                out.note = std::string("IMG_Load_IO: ") + SDL_GetError();
                return out;
            }
            const bool ok = surface_to_rgba(surface, out);
            SDL_DestroySurface(surface);
            if (!ok) return out;
            out.note = "decoded by SDL3_image";
            out.ok = true;
            return out;
#else
            // The web build: SDL3_image has no Emscripten port, so libjpeg does the work.
            out.ok = jpeg_decode_rgba(payload, out);
            return out;
#endif
        }
        out.note = std::string(kind_name(kind)) + " is not an image";
        return out;
    }
// The whole-file helpers --pairs-save needs.  Both are deliberately dumb.
    std::vector<std::uint8_t> slurp_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>());
    }
    bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return out.good();
    }
    bool write_ppm(const std::string& path, const Decoded& image) {
        if (!image.ok) return false;
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out << "P6\n" << image.width << " " << image.height << "\n255\n";
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(image.width) * image.height * 3);
        for (std::size_t i = 0;
        i < static_cast<std::size_t>(image.width) * image.height;
        ++i) {
            rgb[i * 3 + 0] = image.rgba[i * 4 + 0];
            rgb[i * 3 + 1] = image.rgba[i * 4 + 1];
            rgb[i * 3 + 2] = image.rgba[i * 4 + 2];
        }
        out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        return out.good();
    }
// Reads the back buffer; must be called before SDL_RenderPresent.
    bool capture(SDL_Renderer* renderer, const std::string& path) {
        SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
        if (shot == nullptr) {
            std::fprintf(stderr, "SDL_RenderReadPixels: %s\n", SDL_GetError());
            return false;
        }
        SDL_Surface* converted = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(shot);
        if (converted == nullptr) {
            std::fprintf(stderr, "SDL_ConvertSurface: %s\n", SDL_GetError());
            return false;
        }
        const int w = converted->w, h = converted->h;
        std::ofstream out(path, std::ios::binary);
        out << "P6\n" << w << " " << h << "\n255\n";
        std::vector<std::uint8_t> row(static_cast<std::size_t>(w) * 3);
        for (int y = 0;
        y < h;
        ++y) {
            const auto* src =            static_cast<const std::uint8_t*>(converted->pixels) + static_cast<std::size_t>(y) * converted->pitch;
            for (int x = 0;
            x < w;
            ++x) {
                row[static_cast<std::size_t>(x) * 3 + 0] = src[static_cast<std::size_t>(x) * 4 + 0];
                row[static_cast<std::size_t>(x) * 3 + 1] = src[static_cast<std::size_t>(x) * 4 + 1];
                row[static_cast<std::size_t>(x) * 3 + 2] = src[static_cast<std::size_t>(x) * 4 + 2];
            }
            out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }
        SDL_DestroySurface(converted);
        std::printf("captured %s (%dx%d)\n", path.c_str(), w, h);
        return out.good();
    }
    SDL_FRect fit_rect_within(int area_w, int area_h, int img_w, int img_h, bool integer_scale) {
        const float s = std::min(static_cast<float>(area_w) / static_cast<float>(img_w),                             static_cast<float>(area_h) / static_cast<float>(img_h));
        float scale = s;
        if (integer_scale && scale >= 1.0f) scale = std::floor(scale);
        if (scale <= 0.0f) scale = s;
        return SDL_FRect{
            0.0f, 0.0f, static_cast<float>(img_w) * scale, static_cast<float>(img_h) * scale
        };
    }
    std::string sht_sibling(const std::string& sht, const char* ext) {
        if (sht.size() < 3) return {
        };
        return sht.substr(0, sht.size() - 3) + ext;
    }
    Sprite resolve_sprite(const Bundle& bundle, const std::string& xml_name, int state) {
        Sprite sprite;
        const auto* slot = bundle.find(xml_name);
        if (slot == nullptr) {
            sprite.note = "sprite definition not found: " + xml_name;
            return sprite;
        }
        const std::vector<std::uint8_t> payload = bundle.read(*slot);
        pple::XmlNode root;
        std::string error;
        if (!pple::xml_parse(payload, root, error)) {
            sprite.note = "xml: " + error;
            return sprite;
        }
        const pple::XmlNode* anim = nullptr;
        int seen = 0;
        for (const pple::XmlNode& c : root.children) {
            if (c.name != "Animation") continue;
            if (seen == state) anim = &c;
            ++seen;
        }
        if (anim == nullptr) {
            sprite.note = "node has no <Animation> at index " + std::to_string(state) + " (" +                      std::to_string(seen) + " present)";
            return sprite;
        }
        sprite.sheet_class = root.child_text("SheetClass");
        sprite.requested = anim->child_text("Texture");
        sprite.frames = std::max(1, std::atoi(anim->child_text("FrameCount").c_str()));
        sprite.per_row = std::max(1, std::atoi(anim->child_text("FramesPerRow").c_str()));
        sprite.prepped = std::atoi(anim->child_text("PreppedForFilter").c_str()) > 0;
        sprite.reg_x = static_cast<float>(std::atof(root.child_text("RegistrationX").c_str()));
        sprite.reg_y = static_cast<float>(std::atof(root.child_text("RegistrationY").c_str()));
        const float off_x = static_cast<float>(std::atof(anim->child_text("SheetOffsetX").c_str()));
        const float off_y = static_cast<float>(std::atof(anim->child_text("SheetOffsetY").c_str()));
        const float src_w = static_cast<float>(std::atof(root.child_text("SourceWidth").c_str()));
        const float src_h = static_cast<float>(std::atof(root.child_text("SourceHeight").c_str()));
        sprite.src = SDL_FRect{
            off_x, off_y, src_w, src_h
        };
        const auto* sheet = bundle.resolve(sprite.requested, sprite.how);
        if (sheet == nullptr) {
            sprite.note = "sheet did not resolve: " + sprite.requested;
            return sprite;
        }
        sprite.resolved = sheet->entry->name;
        sprite.ok = true;
        sprite.note = "state " + std::to_string(state) + ", " + std::to_string(sprite.frames) + " frame(s), " +                  std::to_string(sprite.per_row) + " per row" + (sprite.prepped ? ", prepped for filter" : "");
        return sprite;
    }

} // namespace pple::viewer
