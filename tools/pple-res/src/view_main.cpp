// pple-view — SDL3 viewer for the Purble Place resource DLLs.
//
//   pple-view <pe>... [--resource NAME] [--sprite XML] [--frame N] [--list]
//                     [--shot out.ppm] [--width W] [--height H] [--font PATH] [--all]
//
// Image decoding is ours where it matters (PNG via png.cpp, BMP via SDL after MSZIP
// inflate); SDL3_image is used only for JPEG and SDL3_ttf only for the HUD.
//
// Resource resolution mirrors the game's own loader, read from
// DllFileMgr::LoadResource (0x1000BBF00) and ResourceTextureD3D::CreateTextureFromSheet
// (0x1000A83DC):
//   * '/' is folded to '\'
//   * a name is tried as given, then with ".cab" appended and inflated
//   * a ".sht" sheet is first rewritten to ".jpg", then tried as ".bmp" + ".cab"
//   * audio-style names resolve under "AUDIO\SFX\"
//   * all loaded modules are searched in order
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "pple/cab.hpp"
#include "pple/concentration.hpp"
#include "pple/movie.hpp"
#include "pple/mui.hpp"
#include "pple/wma.hpp"
#include "pple/pe_image.hpp"
#include "pple/png.hpp"
#include "pple/scene.hpp"
#include "pple/xml.hpp"
namespace {

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
    using pple::PeImage;
    using pple::ResourceEntry;
    std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    std::string normalise(const std::string& s) {
        std::string out = lower(s);
        for (char& c : out)        if (c == '/') c = '\\';
        return out;
    }
    enum class Kind {
        Png, Cab, Jpeg, Xml, Audio, Other
    };
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
    // All modules, searched in order, with the loader's resolution rules.
    class Bundle {
        public:
        struct Slot {
            std::size_t module = 0;
            const ResourceEntry* entry = nullptr;
        };
        bool load(const std::string& path, std::string& error) {
            auto pe = PeImage::load(path, error);
            if (!pe) return false;
            for (const ResourceEntry& r : pe->resources()) by_norm_.emplace(r.norm, Slot{
                modules_.size(), &r
            })
            ;
            paths_.push_back(path);
            modules_.push_back(std::move(*pe));
            return true;
        }
        const std::vector<PeImage>& modules() const {
            return modules_;
        }
        const std::string& path(std::size_t i) const {
            return paths_[i];
        }
        const Slot* find(const std::string& name) const {
            const auto it = by_norm_.find(normalise(name));
            return it == by_norm_.end() ? nullptr : &it->second;
        }
        // Returns the slot and how it was found, following the loader's order.
        const Slot* resolve(const std::string& requested, std::string& how) const {
            if (const Slot* s = find(requested)) {
                how = "exact";
                return s;
            }
            const std::string n = normalise(requested);
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".sht") == 0) {
                const std::string stem = requested.substr(0, requested.size() - 4);
                if (const Slot* s = find(stem + ".jpg")) {
                    how = ".sht -> .jpg";
                    return s;
                }
                if (const Slot* s = find(stem + ".bmp.cab")) {
                    how = ".sht -> .bmp.cab";
                    return s;
                }
            }
            if (const Slot* s = find(requested + ".cab")) {
                how = "name + .cab";
                return s;
            }
            if (const Slot* s = find("AUDIO\\SFX\\" + requested)) {
                how = "AUDIO\\SFX\\ + name";
                return s;
            }
            return nullptr;
        }
        std::vector<std::uint8_t> read(const Slot& slot) const {
            return modules_[slot.module].read(*slot.entry);
        }
        private:
        std::vector<PeImage> modules_;
        std::vector<std::string> paths_;
        std::map<std::string, Slot> by_norm_;
    };
    struct Item {
        Bundle::Slot slot;
        Kind kind = Kind::Other;
    };
    struct Decoded {
        std::vector<std::uint8_t> rgba;
        int width = 0;
        int height = 0;
        bool ok = false;
        std::string note;
    };
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
        }
        out.note = std::string(kind_name(kind)) + " is not an image";
        return out;
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
    // AUDIO.XML: <Audio><Music>…</Music><Sounds><EVENT><File>…</File>…</EVENT></Sounds></Audio>.
// Returns the <File> value for an event key, or an empty string.
std::string audio_event_file(const Bundle& bundle, const std::string& event) {
    std::string how;
    const auto* slot = bundle.find("AUDIO\\AUDIO.XML");
    if (slot == nullptr) return {};
    pple::XmlNode root;
    std::string error;
    if (!pple::xml_parse(bundle.read(*slot), root, error)) return {};
    std::string found;
    const auto scan = [&](const pple::XmlNode& group) {
        for (const pple::XmlNode& candidate : group.children) {
            if (candidate.name != event) continue;
            if (const pple::XmlNode* file = candidate.child("File")) found = file->text;
        }
    };
    for (const pple::XmlNode& group : root.children) {
        if (group.name == "Music" || group.name == "Sounds") scan(group);
        else if (group.name == event) {
            if (const pple::XmlNode* file = group.child("File")) found = file->text;
        }
    }
    return found;
}

// Plays interleaved 16-bit PCM once, then drains.  Returns the device name used, or a reason.
std::string play_pcm(const pple::PcmAudio& audio, float volume, std::string& error) {
    if (!audio.ok) {
        error = "no samples";
        return {};
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        error = std::string("SDL_InitSubSystem(AUDIO): ") + SDL_GetError();
        return {};
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = static_cast<int>(audio.channels);
    spec.freq = static_cast<int>(audio.sample_rate);
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        error = std::string("SDL_OpenAudioDeviceStream: ") + SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return {};
    }
    const char* name = SDL_GetCurrentAudioDriver();
    const std::string device = name == nullptr ? "(unknown driver)" : name;
    if (volume != 1.0f) SDL_SetAudioStreamGain(stream, volume);
    SDL_PutAudioStreamData(stream, audio.samples.data(),
                           static_cast<int>(audio.samples.size() * sizeof(std::int16_t)));
    SDL_ResumeAudioStreamDevice(stream);
    // Wait for the queue to drain, with a hard cap so a stuck device cannot hang the tool.
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(audio.seconds() * 1000.0) + 3000;
    while (SDL_GetAudioStreamQueued(stream) > 0 && SDL_GetTicks() < deadline) SDL_Delay(10);
    SDL_DestroyAudioStream(stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return device;
}

// Purble Pairs sound effects, decoded once from the AUDIO.XML manifest and queued on their own
// SDL streams so a click never waits for the previous sound to finish.
//
// The engine plays sounds by "proto": a compile-time index into GameAudio::aSoundEffects.
// GameAudio::LoadGameAudio (0x10009DDF8) fills that array by walking <Sounds> in document order
// and calling Array<SoundEffect*>::SetAt(index, ...), and XmlNode children are elements only --
// comments are not nodes -- so a proto is the element's position among the elements of <Sounds>,
// counting from zero.  Reading the array off AUDIO\AUDIO.XML of PurblePlace.dll that way gives
// the names below, and every proto this file uses was checked against its call site:
//
//   0x6D Con_BoardCleared      Jar::Update 0x1000838A7
//   0x6E Con_Correct           Card::DoMatcher 0x10007D8B5   (the match branch)
//   0x6F Con_GameOver          OnEnterLoseGameState 0x1000825C2 (and again at 0x1000825FF)
//   0x70 Con_Mixer             Card::DoMixerCheck 0x10007DA1C
//   0x71 Con_NewScreen         Board::InitUI 0x10008934D
//   0x72 Con_PowerUp           Card::DoMatcher 0x10007D75D   (exactly one Wild Card)
//   0x73 Con_TileFlip          Card::FlipForward 0x10007CFCD
//   0x74 Con_TileFlip_Incorrect Card::DoMatcher 0x10007D298  (the mismatch branch)
//   0x75 Con_Win               OnEnterWonGameState 0x100082512
//   0x76 Con_PurbleChef        Card::DoMatcher 0x10007D68C   (the Chef's cake sweep)
//   0x77 Con_TimeDelay         Board::MatchMade 0x10008B115, Board::DoTimeBonus 0x10008B1BA
//   0x78 Con_SneakPeek_Starburst SneakPeakArea::HandleEvent 0x100084FEB
//   0x79 Con_SneakPeek_Get     Card::DoMatcher 0x10007D6FD   (the sneak-peek icon match)
//   0x7A Con_SneakPeek_Click   SneakPeakArea::HandleEvent 0x100084EE0
//
// Note that OnWinAnimStop (0x100082670) and OnLoseAnimStop (0x10008272C) play nothing at all:
// Con_BoardCleared comes from the jar, not from the Professor's exit.
struct PairSfx {
    pple::PcmAudio audio;
    SDL_AudioStream* stream = nullptr;
    float volume = 1.0f;
    std::string event;
    std::string resource;
    bool ok = false;
    bool tried = false;
};

const Bundle* g_sfx_bundle = nullptr;
std::map<std::string, PairSfx> g_sfx;
bool g_sfx_ready = false;
bool g_sfx_enabled = true;
std::string g_sfx_driver;

PairSfx& pair_sfx(const std::string& event) {
    static PairSfx missing;
    PairSfx& slot = g_sfx[event];
    if (slot.tried) return slot;
    slot.tried = true;
    slot.event = event;
    if (g_sfx_bundle == nullptr) return slot;
    const Bundle& bundle = *g_sfx_bundle;
    const std::string file = audio_event_file(bundle, event);
    if (file.empty()) return slot;
    std::string resource = "AUDIO\\SFX\\" + file;
    for (char& c : resource)
        if (c == '/') c = '\\';
    std::string how;
    const auto* res = bundle.resolve(resource, how);
    if (res == nullptr) return slot;
    std::string error;
    if (!pple::wma_decode(bundle.read(*res), slot.audio, error)) return slot;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = static_cast<int>(slot.audio.channels);
    spec.freq = static_cast<int>(slot.audio.sample_rate);
    slot.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (slot.stream == nullptr) return slot;
    slot.resource = res->entry->name;
    // The manifest's own <Volume> field, as a fraction.
    std::string vol_text;
    {
        pple::XmlNode root;
        std::string xml_error;
        if (const auto* audio_xml = bundle.find("AUDIO\\AUDIO.XML");
            audio_xml != nullptr && pple::xml_parse(bundle.read(*audio_xml), root, xml_error)) {
            const auto find_volume = [&](const pple::XmlNode& group) {
                for (const pple::XmlNode& candidate : group.children)
                    if (candidate.name == event) vol_text = candidate.child_text("Volume");
            };
            for (const pple::XmlNode& group : root.children)
                if (group.name == "Music" || group.name == "Sounds") find_volume(group);
        }
    }
    const int volume = vol_text.empty() ? 100 : std::atoi(vol_text.c_str());
    slot.volume = static_cast<float>(volume) / 100.0f;
    SDL_SetAudioStreamGain(slot.stream, slot.volume);
    slot.ok = true;
    return slot;
}

void play_pair_sfx(const std::string& event) {
    if (!g_sfx_enabled || !g_sfx_ready) return;
    PairSfx& sfx = pair_sfx(event);
    if (!sfx.ok || sfx.stream == nullptr) return;
    SDL_PutAudioStreamData(sfx.stream, sfx.audio.samples.data(),
                           static_cast<int>(sfx.audio.samples.size() * sizeof(std::int16_t)));
    SDL_ResumeAudioStreamDevice(sfx.stream);
}

// The sounds one click owes, in the order Card::DoMatcher reaches them.  Card::FlipForward
// (0x10007CFA4) plays Con_TileFlip for every card, including the first of a pair; everything
// else is inside DoMatcher's `v6 != 0` block, i.e. only once the second card has been revealed.
void play_click_sfx(const pple::ClickResult& r) {
    play_pair_sfx("Con_TileFlip");
    if (!r.second) return;
    if (r.mismatch) play_pair_sfx("Con_TileFlip_Incorrect");
    if (r.chef) play_pair_sfx("Con_PurbleChef");
    if (r.sneak_get) play_pair_sfx("Con_SneakPeek_Get");
    if (r.wild_match) play_pair_sfx("Con_PowerUp");
    if (r.match) play_pair_sfx("Con_Correct");
    if (r.mixer) play_pair_sfx("Con_Mixer");
    if (r.time_bonus > 0) play_pair_sfx("Con_TimeDelay");
}

// A sprite node definition, resolved to a sheet and one frame rectangle.
    struct Sprite {
        bool ok = false;
        std::string requested;
        std::string resolved;
        std::string how;
        std::string sheet_class;
        SDL_FRect src{
        };
        int frames = 1;
        int per_row = 1;
        bool prepped = false;
        // NodeSprite::UpdateZero (0x100093740) subtracts scale * reg / 100 from the world rect,
        // so the drawn corner is (x, y) - reg * scale / 100, not (x, y) + reg.
        float reg_x = 0.0f;
        float reg_y = 0.0f;
        std::string note;
    };
    // ResourceTextureD3D::CreateTextureFromSheet (0x1000A83DC) reads a ".sht" name, overwrites
    // the three-character extension with "jpg" and loads that as the colour sheet, overwrites it
    // again with "bmp" and loads that as the alpha sheet, and composes
    // ARGB = (alpha_byte << 24) | (colour_dword & 0xFFFFFF).  The ".bmp" name only resolves at
    // all through the ".cab" fallback in DllFileMgr::LoadResource, i.e. as <stem>.BMP.CAB.
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
    void usage() {
        std::fprintf(stderr,                 "pple-view — SDL3 viewer for the Purble Place resource DLLs\n\n"                 "  pple-view <pe>... [options]\n\n"                 "  --resource NAME   start on this resource\n"                 "  --sprite XML      show one frame of a <Node Type=\"Sprite\"> definition\n"                 "  --scene XML       render a UI layout: every instance with its sheet frame\n"                 "  --movie XML       render a <Movie> skeletal animation at one frame\n"                 "  --movie-name NAME in --scene mode, draw this <Movie> animation slot at --anchor\n"                 "  --pairs [D,L,S]   play Purble Pairs: D = Beginner|Intermediate|Advanced, L = level, S = seed\n"                 "  --menu            start on the real main menu (UI\\PurblePlace\\PurblePlaceManager.xml); its\n"                 "                    Purble Pairs hot-spot enters the game\n"                 "  --mce             draw the Media Center build's New Game / Exit Game header buttons.\n"                 "                    The shipped executable deletes those five nodes when\n"                 "                    g_bMediaCenter == 0 (0x10008122B), so by default they are absent, as\n"                 "                    they are in this build\n"                 "  --no-container-offsets  do not apply the engine's HUD re-parenting (notes 17.7)\n"                 "  --anchor X,Y      origin for --movie, or for --movie-name in --scene mode\n"
                 "  --play NAME       decode and play an AUDIO.XML event key or a resource name\n"
                 "  --volume N        playback volume, 0..100 (default 100)\n"
                 "  --mute            silence the Purble Pairs sound effects\n"
                 "  --pairs-click I,J  flip these cell indices, through the mouse handler\n"                 "  --frame N         frame index within the sprite (default 0)\n"                 "  --state N         which <Animation> of a Button node to show (default 0;\n"                 "                     the engine starts at 0 and moves to 1 on <enter>, 2 on <down>)\n"                 "  --list            list viewable resources and exit\n"                 "  --shot FILE.ppm   render one frame offscreen, write it, exit\n"                 "  --width N --height N   window size (default 1400x900)\n"                 "  --font PATH       TTF for the HUD (default C:\\Windows\\Fonts\\arial.ttf)\n"                 "  --all             include resources this viewer cannot decode\n\n"                 "  keys: LEFT/RIGHT or PGUP/PGDN navigate, SPACE toggles integer scaling,\n"                 "        in --menu/--pairs: click a hot-spot, N new game, R reshuffle, ESC quits\n"                 "        (ESC in the Concentration screen returns to the menu)\n");
    }
    std::size_t parse_index(const char* text) {
        return static_cast<std::size_t>(std::strtoul(text, nullptr, 10));
    }
}
// namespace
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        usage();
        return args.empty() ? 1 : 0;
    }
    std::vector<std::string> modules;
    std::string want_resource;
    std::string sprite_xml;
    std::string scene_xml;
    std::string movie_xml;
    std::string movie_name;
    std::string pairs_spec;
    std::string mui_path;
    std::string play_name;
    float play_volume = 1.0f;
    bool mute = false;
    std::string pairs_click;
    std::string shot_path;
    std::string font_path = "C:\\Windows\\Fonts\\arial.ttf";
    std::string save_path;
    int width = 1400, height = 900;
    std::size_t frame = 0;
    int state = 0;
    float anchor_x = 0.0f, anchor_y = 0.0f;
    unsigned seed_for_pairs = 20261005u;
    bool pairs_reveal = false;
    bool list_only = false, include_all = false;
    bool menu_mode = false;
    bool mce_mode = false;
    bool container_offsets = true;
    std::string script_click_spec;
    int pairs_ticks = 0;
    bool pairs_solve = false;
    int shot_delay_ms = 0;
    int bench_frames = 0;
    int bench_build = 0;
    for (std::size_t i = 0;
    i < args.size();
    ++i) {
        const std::string& a = args[i];
        if (a == "--resource" && i + 1 < args.size()) want_resource = args[++i];
        else if (a == "--sprite" && i + 1 < args.size()) sprite_xml = args[++i];
        else if (a == "--scene" && i + 1 < args.size()) scene_xml = args[++i];
        else if (a == "--movie" && i + 1 < args.size()) movie_xml = args[++i];
        else if (a == "--movie-name" && i + 1 < args.size()) movie_name = args[++i];
        else if (a == "--pairs") pairs_spec = (i + 1 < args.size() && args[i + 1][0] != '-') ? args[++i] : "Beginner,0";
        else if (a == "--pairs-reveal") pairs_reveal = true;
        else if (a == "--menu") menu_mode = true;
        else if (a == "--mce") mce_mode = true;
        else if (a == "--no-container-offsets") container_offsets = false;
        else if (a == "--script-click" && i + 1 < args.size()) script_click_spec = args[++i];
        else if (a == "--shot-delay" && i + 1 < args.size()) shot_delay_ms = std::atoi(args[++i].c_str());
        else if (a == "--bench" && i + 1 < args.size()) bench_frames = std::atoi(args[++i].c_str());
        else if (a == "--bench-build" && i + 1 < args.size()) bench_build = std::atoi(args[++i].c_str());
        else if (a == "--pairs-ticks" && i + 1 < args.size()) pairs_ticks = std::atoi(args[++i].c_str());
        else if (a == "--pairs-solve") pairs_solve = true;
        else if (a == "--mui" && i + 1 < args.size()) mui_path = args[++i];
        else if (a == "--play" && i + 1 < args.size()) play_name = args[++i];
        else if (a == "--volume" && i + 1 < args.size()) play_volume = static_cast<float>(std::atof(args[++i].c_str())) / 100.0f;
        else if (a == "--mute") mute = true;
        else if (a == "--pairs-click" && i + 1 < args.size()) pairs_click = args[++i];
        else if (a == "--anchor" && i + 1 < args.size()) {
            const std::string v = args[++i];
            const std::size_t comma = v.find(',');
            if (comma != std::string::npos) {
                anchor_x = static_cast<float>(std::atof(v.substr(0, comma).c_str()));
                anchor_y = static_cast<float>(std::atof(v.substr(comma + 1).c_str()));
            }
        }        else
        if (a == "--frame" && i + 1 < args.size()) frame = parse_index(args[++i].c_str());
        else if (a == "--state" && i + 1 < args.size()) state = std::atoi(args[++i].c_str());
        else if (a == "--shot" && i + 1 < args.size()) shot_path = args[++i];
        else if (a == "--font" && i + 1 < args.size()) font_path = args[++i];
        else if (a == "--save" && i + 1 < args.size()) save_path = args[++i];
        else if (a == "--width" && i + 1 < args.size()) width = std::atoi(args[++i].c_str());
        else if (a == "--height" && i + 1 < args.size()) height = std::atoi(args[++i].c_str());
        else if (a == "--list") list_only = true;
        else if (a == "--all") include_all = true;
        else if (!a.empty() && a[0] != '-') modules.push_back(a);
    }
    if (modules.empty()) {
        usage();
        return 1;
    }
    // Load the .mui first (if asked), so every label resolved afterwards uses it.
    pple::MuiStrings mui_strings;
    if (!mui_path.empty()) {
        std::string mui_error;
        const auto mui_pe = PeImage::load(mui_path, mui_error);
        if (!mui_pe) {
            std::fprintf(stderr, "%s: %s\n", mui_path.c_str(), mui_error.c_str());
            return 1;
        }
        if (!pple::mui_load(*mui_pe, mui_strings, mui_error)) {
            std::fprintf(stderr, "%s: %s\n", mui_path.c_str(), mui_error.c_str());
            return 1;
        }
        g_mui_for_labels = &mui_strings;
        pple::set_localiser(mui_resolve, nullptr);
        std::printf("mui: %s, %zu strings, installed as the label localiser\n", mui_path.c_str(),
                    mui_strings.non_empty);
    }
    Bundle bundle;
    for (const std::string& m : modules) {
        std::string error;
        if (!bundle.load(m, error)) {
            std::fprintf(stderr, "%s: %s\n", m.c_str(), error.c_str());
            return 1;
        }
    }
    std::vector<Item> items;
    std::size_t skipped = 0;
    for (std::size_t m = 0;
    m < bundle.modules().size();
    ++m) {
        const PeImage& pe = bundle.modules()[m];
        for (const ResourceEntry& entry : pe.resources()) {
            Item item;
            item.slot = Bundle::Slot{
                m, &entry
            };
            const std::uint32_t head_size = std::min<std::uint32_t>(entry.size, 64);
            std::vector<std::uint8_t> head(head_size, 0);
            if (entry.file_offset != 0 &&                static_cast<std::size_t>(entry.file_offset) + head_size <= pe.file_bytes().size())                std::memcpy(head.data(), pe.file_bytes().data() + entry.file_offset, head_size);
            item.kind = classify(head);
            const bool decodable = item.kind == Kind::Png || item.kind == Kind::Cab || item.kind == Kind::Jpeg;
            if (!decodable && !include_all) {
                ++skipped;
                continue;
            }
            items.push_back(item);
        }
    }
    if (!play_name.empty()) {
        // --play resolves the name as an AUDIO.XML event first, then as a resource name, so the
        // whole manifest -> resource -> Media Foundation -> SDL chain is exercised in one call.
        std::string resource = play_name;
        std::string event_file = audio_event_file(bundle, play_name);
        std::string how;
        if (!event_file.empty()) {
            resource = "AUDIO\\SFX\\" + event_file;
            for (char& c : resource)
                if (c == '/') c = '\\';
            std::printf("event %s -> %s\n", play_name.c_str(), event_file.c_str());
        }
        const auto* slot = bundle.resolve(resource, how);
        if (slot == nullptr) {
            std::fprintf(stderr, "cannot resolve audio resource: %s\n", resource.c_str());
            return 1;
        }
        pple::PcmAudio audio;
        std::string error;
        if (!pple::wma_decode(bundle.read(*slot), audio, error)) {
            std::fprintf(stderr, "decode failed: %s\n", error.c_str());
            return 1;
        }
        std::printf("decoded  %s (%s): %uch %u Hz, %zu frames, %.3f s\n", slot->entry->name.c_str(),
                    how.c_str(), audio.channels, audio.sample_rate, audio.frames(), audio.seconds());
        std::string play_error;
        const std::string device = play_pcm(audio, play_volume, play_error);
        if (!play_error.empty()) {
            std::fprintf(stderr, "playback failed: %s\n", play_error.c_str());
            return 1;
        }
        std::printf("played   through SDL audio driver \"%s\" at volume %.0f%%\n", device.c_str(),
                    static_cast<double>(play_volume * 100.0f));
        return 0;
    }
    if (items.empty() && sprite_xml.empty() && scene_xml.empty()) {
        std::fprintf(stderr, "no decodable resources\n");
        return 1;
    }
    std::size_t index = 0;
    if (!want_resource.empty()) {
        const std::string want = normalise(want_resource);
        bool found = false;
        for (std::size_t i = 0;
        i < items.size();
        ++i)            if (items[i].slot.entry->norm == want) {
            index = i;
            found = true;
            break;
        }
        if (!found) std::fprintf(stderr, "resource not found, starting at the first: %s\n", want_resource.c_str());
    }
    if (list_only) {
        for (std::size_t i = 0;
        i < items.size();
        ++i)            std::printf("%4zu  %-12s %9u  %s\n", i, kind_name(items[i].kind), items[i].slot.entry->size,                        items[i].slot.entry->name.c_str());
        std::printf("-- %zu viewable items, %zu non-image resources skipped\n", items.size(), skipped);
        return 0;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const bool offscreen = !shot_path.empty();
    SDL_Window* window = SDL_CreateWindow("pple-view", width, height,                                         offscreen ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, bench_frames > 0 ? 0 : 1);
    std::printf("renderer: %s\n", SDL_GetRendererName(renderer));
    TTF_Font* font = nullptr;
    if (TTF_Init()) {
        font = TTF_OpenFont(font_path.c_str(), 15.0f);
        if (font == nullptr)            std::fprintf(stderr, "TTF_OpenFont(%s): %s\n", font_path.c_str(), SDL_GetError());
    } else
    {
        std::fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
    }
    Decoded current;
    SDL_Texture* texture = nullptr;
    SDL_FRect src_rect{
    };
    std::vector<std::string> hud_lines;
    std::vector<SDL_Texture*> hud_textures;
    int hud_w = 0, hud_h = 0;
    bool integer_scale = false;  // SPACE toggles; off means the scene fills the window
    // Scene mode: a flat draw list in scene coordinates, plus the scene's extent.
    struct DrawItem {
        SDL_Texture* texture = nullptr;
        SDL_FRect src{
        };
        SDL_FRect dst{
        };
        int alpha = 255;
        float rotation = 0.0f;
        // degrees clockwise, about the centre of dst
    };
    std::vector<DrawItem> scene_draws;
    float scene_w = 0.0f, scene_h = 0.0f;
    // Set by the render loop, read back by the click handler to invert scene -> window.
    float view_scale = 1.0f, view_ox = 0.0f, view_oy = 0.0f;
    std::map<int, TTF_Font*> font_cache;
    const auto font_for = [&](float size) -> TTF_Font* {
        const int key = static_cast<int>(size + 0.5f);
        const auto it = font_cache.find(key);
        if (it != font_cache.end()) return it->second;
        TTF_Font* f = TTF_OpenFont(font_path.c_str(), static_cast<float>(std::max(6, key)));
        font_cache[key] = f;
        return f;
    };
    // One texture per line; rebuilt only when the item changes.
    const auto rebuild_hud = [&]() {
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
    };
    // Sheet textures, cached by resolved resource name; a scene reuses them heavily.
    // Alongside each texture a coarse 64x64 occupancy grid is kept, so a declared frame
    // rectangle can be checked for actually containing art.
    std::map<std::string, SDL_Texture*> sheet_cache;
    std::map<std::string, std::pair<int, int>> sheet_sizes;
    std::map<std::string, std::vector<std::uint8_t>> sheet_coverage;
    // Resource names the last sheet load went through, for the HUD and the report.
    std::vector<std::string> named_steps;
    // `alpha_only` is set for sheets composed from a colour/alpha pair: their background is
    // genuinely transparent, so the alpha channel alone decides occupancy.  A plain JPEG has
    // no alpha and paints its empty area flat black, so both signals are needed there.
    const auto build_coverage = [](SDL_Surface* surface, std::vector<std::uint8_t>& grid,                                   bool alpha_only) {
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
    };
    // Fraction of the sheet's occupancy grid that falls inside a declared frame rect.
    const auto rect_coverage = [&](const std::string& sheet, const SDL_FRect& src) -> float {
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
    };
    const auto load_sheet_texture = [&](const std::string& requested, const std::string& resolved_name,                                        std::string& note) -> SDL_Texture* {
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
    };
    // Movie mode: evaluate the skeletal timeline at one frame and lay every part out as a
    // sprite draw.  Positions in a <Movie> are the movie's own coordinate space, so `anchor`
    // is where that space's origin lands.
    // Draw-list teardown.  Sprite and sheet textures belong to sheet_cache and must survive a
    // rebuild; only the directly created ones (labels) are freed here.
    const auto release_draws = [&]() {
        for (const DrawItem& d : scene_draws) {
            if (d.texture == nullptr) continue;
            bool cached = false;
            for (const auto& entry : sheet_cache)
                if (entry.second == d.texture) cached = true;
            if (!cached) SDL_DestroyTexture(d.texture);
        }
        scene_draws.clear();
    };
    const auto build_movie = [&]() {
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
    };
    // One shared resource fetch, in the shape pple::scene_load / pple::movie_load want.
    const auto fetch_resource = [&](const std::string& name, std::vector<std::uint8_t>& out) {
        std::string inner_how;
        const auto* s = bundle.resolve(name, inner_how);
        if (s == nullptr) return false;
        out = bundle.read(*s);
        return true;
    };
    std::optional<pple::Scene> loaded_scene;
    // Instances build_scene must not draw.  The Concentration document carries three time-bar
    // elements that are not draws: <TimeBarGreen>/<TimeBarRed> are the segment templates the
    // engine clones 226 times (notes 17.7), and <TimeBarBkg> is hidden outright on an untimed
    // difficulty (TimeBar::InitUI 0x10008538E).
    std::set<std::string> hidden_instances;
    // A sprite drawn behind everything the document describes.  The Concentration room is not in
    // the scene document: ConcentrationGame::LoadGameUI (0x100081042) creates
    // UI\Concentration\hearts.xml itself as a child of the game's parent node, so it lands at
    // 0,0 and is Background.png, 800x496.
    std::string scene_background_xml;
    // The index in scene_draws of the /JarFg sprite, so the jar's tray coins can be inserted
    // between /JarBkg and it.  Set by build_scene.
    int pairs_jar_front_at = -1;
    // Builds the scene draw list: every described instance resolved to a sheet frame,
    // plus labels rendered through TTF.
    const auto build_scene = [&]() {
        release_draws();
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
                if (key == "JarFg") pairs_jar_front_at = static_cast<int>(scene_draws.size()) - 1;
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
    };
    // ---- Purble Pairs -----------------------------------------------------------------
    // The board data and rules live in pple::ConcentrationBoard; this only places the cards.
    pple::ConcentrationBoard pairs_board;
    bool pairs_ready = false;
    pple::CardPools pairs_pools;
    pple::CardsBox pairs_box;
    // Kept so the click handler can reach the constants the peek timers use.
    pple::ConcentrationConstants pairs_constants;
    // The sneak-peek token rects, one per <NumStartTokens>, at /SneakPeekContainer.
    std::vector<SDL_FRect> pairs_tokens;
    // The progress meter: /ProgressMeterContainer (665,40) and one indicator per <Level>, stacked
    // /ProgressMeterIndicators/yOffSet (55) apart, drawn from the CardBack <progpath> sprites.
    struct MeterDraw {
        SDL_Texture* tex = nullptr;
        SDL_FRect src_unlit{};
        SDL_FRect src_lit{};
        SDL_FRect dst{};
    };
    std::vector<MeterDraw> pairs_meter;
    // The jar's tray: one coin sprite per <Level> (`CardBacks.xml`'s /imagepath, drawn at the 38
    // percent scale Jar::DoFiller sets) placed at that level's *_Points.xml coordinates.
    struct JarCoin {
        SDL_Texture* tex = nullptr;
        SDL_FRect src{};
        float w = 0.0f, h = 0.0f;
        float reg_x = 0.0f, reg_y = 0.0f;  // already scaled by 0.38
        float x = 0.0f, y = 0.0f;          // inside /JarContainer
        float rotation = 0.0f;
    };
    std::vector<JarCoin> pairs_jar_coins;
    float pairs_jar_origin_x = 0.0f, pairs_jar_origin_y = 0.0f;
    // The same `/imagepath` sprite unscaled, which is what an in-flight coin draws.
    SDL_Texture* pairs_coin_tex = nullptr;
    SDL_FRect pairs_coin_src{};
    float pairs_coin_w = 0.0f, pairs_coin_h = 0.0f, pairs_coin_reg_x = 0.0f, pairs_coin_reg_y = 0.0f;
    // The jar's flying coins.  Board::DoFlyIntoJar (0x10008AF54) spawns one ArcBehavior per
    // matched card and ArcBehavior::Init (0x100083E48) gives it three points -- source, an apex,
    // destination -- which ArcBehavior::Update (0x100084128) walks as a quadratic Bezier over
    // /ArcAnimation/Frames ticks.  All three are in the ArcContainer's own space, which
    // Jar::InitUI (0x100083218) creates as a plain "Base" child of /JarContainer at 0,0, so the
    // scene coordinates below are /JarContainer's origin plus the point.
    struct ArcCoin {
        SDL_Texture* tex = nullptr;
        SDL_FRect src{};
        float sprite_w = 0.0f, sprite_h = 0.0f;
        float reg_x = 0.0f, reg_y = 0.0f;
        float sx = 0.0f, sy = 0.0f;  // source: the card's scene position (+2 in y)
        float cx = 0.0f, cy = 0.0f;  // the Bezier control point
        float dx = 0.0f, dy = 0.0f;  // destination: (LidOffSet + CurJar.w/2, CurJar.h/2)
        float scale0 = 100.0f;       // Board+0x170 + 10, in percent
        int tick = 0;
    };
    std::vector<ArcCoin> pairs_arcs;
    int pairs_arc_frames = 25;  // /ArcAnimation/Frames
    float pairs_arc_lid = -15.0f;  // /ArcAnimation/LidOffSet
    float pairs_curjar_w = 112.0f, pairs_curjar_h = 112.0f;  // /JarFg ("CurJar")
    // Jar+0xC8, the number of coins on the tray.  Jar::DoFiller (0x100083B10) grows it from
    // `points / Board+0x28 * Round+0x12C`, and both of those are the CURRENT level's numbers
    // (Board::InitLevel 0x1000894E0 zeroes Round+0x12C and sets Board+0x28 to this level's card
    // count), so the fill restarts at each level rather than accumulating over the round.
    int pairs_jar_fill = 0;
    // Jar::Update (0x10008373C) in three states: the coins fly, then the level's burst movie
    // plays, then Jar::StopExplosion (0x100083A20) ends the board.
    enum class JarPhase { Idle, Flying, Burst };
    JarPhase pairs_jar_phase = JarPhase::Idle;
    // The level's burst movie: CardBacks.xml's /moviepath, loaded by Jar::LoadMovies
    // (0x1000835D0) and parented to /JarMovieContainer at -15,-425 inside /JarContainer.
    struct BurstMovie {
        bool ok = false;
        pple::Movie movie;
    };
    std::map<std::string, BurstMovie> pairs_burst_cache;
    std::string pairs_burst_name;  // the current level's /moviepath
    float pairs_burst_anchor_x = 0.0f, pairs_burst_anchor_y = 0.0f;
    float pairs_burst_frame = 0.0f;
    // Loads a level's burst movie once and caches it.  Jar::LoadMovies does this at Jar::InitUI,
    // but the document is 250 KB of keyframes (88 frames, 322 parts), so it is deferred to the
    // moment the burst starts rather than paid on every scene rebuild.
    const auto load_burst = [&](const std::string& name) -> const BurstMovie* {
        if (name.empty()) return nullptr;
        const auto found = pairs_burst_cache.find(name);
        if (found != pairs_burst_cache.end()) return found->second.ok ? &found->second : nullptr;
        BurstMovie entry;
        std::vector<std::uint8_t> bytes;
        const auto fetch = [&](const std::string& part_name, std::vector<std::uint8_t>& out) {
            std::string how;
            const auto* s = bundle.resolve(part_name, how);
            if (s == nullptr) return false;
            out = bundle.read(*s);
            return true;
        };
        std::string error;
        if (fetch(name, bytes) && pple::movie_load(bytes, fetch, entry.movie, error)) {
            entry.ok = !entry.movie.parts.empty();
            const float fps = entry.movie.parts.empty()
                                  ? entry.movie.fps
                                  : pple::movie_frame_rate(entry.movie, entry.movie.parts.front());
            std::printf("  jar burst: %s, %d frame(s) at %.0f fps, %zu part(s), anchor %.0f,%.0f\n",
                        name.c_str(), entry.movie.frame_count, static_cast<double>(fps),
                        entry.movie.parts.size(), static_cast<double>(pairs_burst_anchor_x),
                        static_cast<double>(pairs_burst_anchor_y));
        } else {
            std::printf("  jar burst: %s did not load (%s)\n", name.c_str(), error.c_str());
        }
        const auto placed = pairs_burst_cache.emplace(name, std::move(entry));
        return placed.first->second.ok ? &placed.first->second : nullptr;
    };
    // The PlayArea node's placement, so a mouse position can be turned back into a cell.
    float pairs_origin_x = 0.0f, pairs_origin_y = 0.0f, pairs_outer_scale = 1.0f;
    std::string pairs_sheet_note;
    std::string pairs_back_xml = "UI\\Concentration\\blankcardbutton.xml";
    std::string pairs_outline = "UI\\Concentration\\highlite_outline.xml";
    // The card draw list is built once, from a table of resolved sprites, and only the face /
    // highlight choice is rebuilt afterwards.  Before this, every tick of the reveal timer
    // re-parsed ~25 card node definitions and reloaded the sheets, up to 30 times a second.
    struct CardCache {
        SDL_Texture* tex_face = nullptr;
        SDL_FRect src_face{};
        SDL_Texture* tex_back = nullptr;
        SDL_FRect src_back{};
        float base_x = 0.0f, base_y = 0.0f;  // scene position of the cell's top-left
        float w = 0.0f, h = 0.0f;            // drawn size at the card's own scale
        float cell_w = 0.0f, cell_h = 0.0f;  // the click target and the flip's centring reference
        int index = -1;
    };
    std::vector<CardCache> card_cache;
    // How many draws belong to the scene; everything past this is the card layer.
    std::size_t pairs_scene_draws = 0;
    // Card flip, from Card::UpdateFlipForward (0x10007EA20) and UpdateFlipBackward (0x10007EFEC):
    // the visible side narrows by 17 percent per tick to a 15 percent floor, then the other side
    // widens by 17; the shadow tracks at 5 and the vertical scale drifts by 2 either way.  The
    // phase decides which side is drawn.
    enum class FlipPhase { None, Collapse, Expand };
    struct CardFlip {
        FlipPhase phase = FlipPhase::None;
        int scale_x = 100;
        int scale_y = 100;
        bool to_face = true;  // forward flip: the back collapses and the face expands
    };
    std::vector<CardFlip> card_flip;
    std::vector<pple::CardFace> card_face_shown;  // what the board said last time we drew
    // The time bar is engine-made, not scene-made: <TimeBarGreen>/<TimeBarRed> are 226 1-pixel
    // segments stacked by CreateTimeSlice (0x10008C640), and /TimeBarBkg is a plain draw.
    struct TimeBarDraw {
        bool ok = false;
        SDL_Texture* green = nullptr;
        SDL_FRect green_src{};
        SDL_Texture* red = nullptr;
        SDL_FRect red_src{};
        float x_green = 0.0f, x_red = 0.0f, y_top = 0.0f;
        int segments = 0;
    };
    TimeBarDraw timebar;
    // The Professor is engine-built too: Professor::InitUI (0x10008BD94) creates a Base from
    // /ProfessorContainer and loads all six Professor movies as its children, so the movie's own
    // coordinate space is anchored at the container's position.  The scene's nine movie slots
    // carry no position at all.
    struct ProfessorDraw {
        bool ok = false;
        pple::Movie movie;
        std::string which;
        float anchor_x = 0.0f, anchor_y = 0.0f;
        float time = 0.0f;
    };
    ProfessorDraw professor;
    // Live HUD text.  Kept here so a tick can refresh it without rebuilding the board.
    std::string pairs_title = "Purble Pairs";
    const auto refresh_hud = [&]() {
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
    };
    // Assigned once build_pairs exists; build_pairs itself only needs to call it.
    std::function<void()> refresh_cards;
    // --bench measures the card layer: refresh_cards is what replaced the per-tick build_pairs,
    // so its cost per call is the number that matters.
    double refresh_total_ms = 0.0;
    std::size_t refresh_calls = 0;
    std::function<void()> timed_refresh;
    double build_ms = 0.0;
    std::size_t build_calls = 0;
    std::function<bool(const std::string&)> load_professor;
    // Which screen is up, and the clickable rectangles on each.  A hot-spot rect is the engine's
    // node rect: (x, y) - registration, sized SourceWidth x SourceHeight.
    enum class Screen { Other, Menu, Concentration };
    Screen screen = Screen::Other;
    struct Hotspot {
        std::string name;
        SDL_FRect rect{};
    };
    std::vector<Hotspot> menu_hotspots;
    std::vector<Hotspot> conc_hotspots;
    bool menu_ready = false;
    std::string menu_hover_name;
    // Set when build_pairs must rebuild the art for a board that already moved on: the Mixer
    // reshuffled the cards, or Board::NewLevel advanced to the next <Level>.
    bool pairs_keep_state = false;
    // One end-of-round sound per round.
    bool pairs_end_played = false;
    // Set by a click that changed the layout; the main loop does the rebuild.
    bool pairs_rebuild_pending = false;
    // Jar::Update (0x10008389D): the flying-coin array is empty and the board is complete, so
    // play Con_BoardCleared (proto 0x6D), put /JarBkg behind everything and show the level's
    // burst movie (Jar+0x30, from CardBacks.xml's /moviepath).
    const auto jar_begin_burst = [&]() {
        if (!pairs_board.end_pending()) return;
        play_pair_sfx("Con_BoardCleared");
        pairs_burst_frame = 0.0f;
        const BurstMovie* burst = load_burst(pairs_burst_name);
        pairs_jar_phase = (burst != nullptr && burst->movie.frame_count > 0) ? JarPhase::Burst
                                                                            : JarPhase::Idle;
    };
    // Jar::StopExplosion (0x100083A20): the board has finished bursting, so either the round is
    // won or Board::NewLevel deals the next <Level>.  The scripted hooks call this directly,
    // because they have no frames in which to animate the flight and the burst.
    const auto finish_board_end = [&]() {
        if (!pairs_board.end_pending()) return;
        const pple::ClickResult r = pairs_board.end_board();
        pairs_arcs.clear();
        pairs_jar_phase = JarPhase::Idle;
        pairs_burst_frame = 0.0f;
        if (r.level_up) {
            std::printf("    board relaid out -> level %d/%d\n", pairs_board.level_position() + 1,
                        pairs_board.level_count());
            pairs_rebuild_pending = true;
            pairs_keep_state = true;
        } else if (r.board_cleared) {
            std::printf("  board cleared -> round WON at level %d/%d\n",
                        pairs_board.level_position() + 1, pairs_board.level_count());
        }
    };
    // One movie's parts appended to `out` at an anchor.  PurblePlaceMovie::InitFromXml
    // (0x100087748) creates every <ImageDef> part as a child of the node it is handed, so a part
    // draws at (anchor + part) - registration.
    const auto append_movie = [&](const pple::Movie& mv, int movie_frame, float ax, float ay,
                                  std::vector<DrawItem>& out, float& extent_w,
                                  float& extent_h) -> int {
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
    };
    // Board::DoFlyIntoJar (0x10008AF54) once per matched card.  The source is the card's own
    // position -- Card::Update (0x10007DAFA) passes the card node's (x, y), which is
    // <PlayArea>-relative, and the function adds PlayArea's 150,2 back while subtracting
    // /JarContainer's origin, so the arc runs in /JarContainer's space.
    const auto spawn_arcs = [&](const std::vector<int>& cells) {
        if (pairs_coin_tex == nullptr || cells.empty()) return;
        const float dx = pairs_jar_origin_x + pairs_arc_lid + pairs_curjar_w * 0.5f;
        const float dy = pairs_jar_origin_y + pairs_curjar_h * 0.5f;
        for (int cell : cells) {
            if (cell < 0 || cell >= static_cast<int>(pairs_board.slots().size())) continue;
            const pple::ConcentrationSlot& slot = pairs_board.slots()[static_cast<std::size_t>(cell)];
            ArcCoin c;
            c.tex = pairs_coin_tex;
            c.src = pairs_coin_src;
            c.sprite_w = pairs_coin_w;
            c.sprite_h = pairs_coin_h;
            c.reg_x = pairs_coin_reg_x;
            c.reg_y = pairs_coin_reg_y;
            c.sx = pairs_origin_x + static_cast<float>(slot.x);
            c.sy = pairs_origin_y + static_cast<float>(slot.y) + 2.0f;
            c.dx = dx;
            c.dy = dy;
            // ArcBehavior+0x28 is `card_y + 62` (0x10008B052) and the apex is
            // `min(src.y, dst.y) - ArcBehavior+0x28` (0x1000840E8) in the arc's own space.  The
            // source's y is `card_y + 2 - JarContainer.y`, so the two card_y terms cancel and the
            // control point's height above the container is a constant 375 px for every card at
            // or above the jar.  The control point is off the top of the 800x496 scene, but the
            // drawn curve is not: a quadratic Bezier only reaches about half way to its control
            // point, so the coin's highest drawn point stays on screen.
            c.cx = dx + (c.sx - dx) * 0.5f;
            c.cy = std::min(c.sy, c.dy) - static_cast<float>(slot.y) - 62.0f;
            c.scale0 = static_cast<float>(slot.scale_percent + 10);  // Board+0x170 + 10
            c.tick = 0;
            pairs_arcs.push_back(c);
        }
        if (!pairs_arcs.empty()) pairs_jar_phase = JarPhase::Flying;
    };
    // Jar::Update's landing: the tray grows to `points / Board+0x28 * Round+0x12C`.  DoFiller
    // only ever adds nodes, so the count never falls within a level.
    const auto jar_grow = [&]() {
        if (pairs_jar_coins.empty()) return;
        const int total_cards = std::max(1, pairs_board.cell_count());
        const double want = static_cast<double>(pairs_jar_coins.size()) /
                            static_cast<double>(total_cards) *
                            static_cast<double>(pairs_board.cards_removed());
        pairs_jar_fill = std::max(pairs_jar_fill, static_cast<int>(want));
    };
    const auto build_pairs = [&]() {
        const Uint64 build_t0 = SDL_GetPerformanceCounter();
        std::string scene_how;
        const auto* scene_slot = bundle.resolve(scene_xml, scene_how);
        const auto* cards_slot = bundle.find("UI\\CONCENTRATION\\CARDS.XML");
        if (scene_slot == nullptr || cards_slot == nullptr) {
            std::fprintf(stderr, "pairs: need UI\\CONCENTRATION\\CONCENTRATIONGAME.XML and CARDS.XML\n");
            return;
        }
        const std::vector<std::uint8_t> scene_bytes = bundle.read(*scene_slot);
        std::string error;
        if (!pple::concentration_pools_parse(bundle.read(*cards_slot), pairs_pools, error)) {
            std::fprintf(stderr, "CARDS.XML: %s\n", error.c_str());
            return;
        }
        pple::ConcentrationConstants constants;
        std::vector<pple::ConcentrationLevel> levels;
        if (!pple::concentration_game_parse(scene_bytes, constants, levels, error)) {
            std::fprintf(stderr, "CONCENTRATIONGAME.XML: %s\n", error.c_str());
            return;
        }
        pple::concentration_cards_box_parse(scene_bytes, pairs_box, error);
        pairs_constants = constants;
        // --pairs "Difficulty,Level[,Seed]"
        std::string want_difficulty = "Beginner";
        int want_level = 0;
        {
            std::vector<std::string> parts;
            std::size_t at = 0;
            while (at <= pairs_spec.size()) {
                const std::size_t comma = pairs_spec.find(',', at);
                parts.push_back(pairs_spec.substr(at, comma - at));
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
            if (!parts.empty() && !parts[0].empty()) want_difficulty = parts[0];
            if (parts.size() > 1 && !parts[1].empty()) want_level = std::atoi(parts[1].c_str());
            if (parts.size() > 2 && !parts[2].empty()) seed_for_pairs = std::stoul(parts[2]);
        }
        const pple::ConcentrationLevel* chosen = nullptr;
        // A rebuild after the Mixer reshuffled the cards or after Board::NewLevel advanced the
        // level must NOT re-deal: the board already carries the new state.  The scene, the
        // Professor and the art are rebuilt, the board is left alone.
        const bool keep_state = pairs_keep_state;
        pairs_keep_state = false;
        if (keep_state && pairs_ready)
            chosen = &pairs_board.level();
        else
            for (const pple::ConcentrationLevel& l : levels)            if (l.difficulty == want_difficulty && l.level_index == want_level) chosen = &l;
        if (chosen == nullptr && !levels.empty()) chosen = &levels.front();
        if (chosen == nullptr) {
            std::fprintf(stderr, "pairs: no level\n");
            return;
        }
        // <TimeBarGreen>/<TimeBarRed> are segment templates, and the bar background is hidden on
        // an untimed difficulty (TimeBar::InitUI 0x10008538E tests difficulty 0 or Board+0x154).
        hidden_instances.clear();
        hidden_instances.insert("TimeBarGreen");
        hidden_instances.insert("TimeBarRed");
        if (!chosen->timed()) hidden_instances.insert("TimeBarBkg");
        scene_background_xml = "UI\\Concentration\\hearts.xml";
        build_scene();
        // Board::Update (0x10008A4F0) drives the end of the round and
        // ConcentrationGame::OnEnterWonGameState (0x1000821D0) / OnEnterLoseGameState
        // (0x10008259C) start the Professor's win and lose movies; OnWinAnimStop
        // (0x100082670) / OnLoseAnimStop (0x10008272C) end them.  The idle movie runs in between.
        professor = ProfessorDraw{};
        if (loaded_scene) {
            for (const pple::SceneItem& item : loaded_scene->containers) {
                const std::string& key = item.name.empty() ? item.instance : item.name;
                if (key == "ProfessorContainer") {
                    professor.anchor_x = item.x;
                    professor.anchor_y = item.y;
                }
            }
        }
        if (!load_professor("ProfessorIdleAnimation"))
            std::printf("  professor: ProfessorIdleAnimation did not load\n");
        else
            std::printf("  professor: %d frame(s), %zu part(s), anchor %.0f,%.0f\n",
                        professor.movie.frame_count, professor.movie.parts.size(),
                        static_cast<double>(professor.anchor_x),
                        static_cast<double>(professor.anchor_y));
        if (!g_sfx_ready) {
            g_sfx_ready = true;
            g_sfx_enabled = !mute;
            g_sfx_bundle = &bundle;
            if (g_sfx_enabled && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                // The three the sound check in the README names, plus every other event this
                // screen can reach.  Each is decoded once, at the manifest's own <Volume>.
                const char* kEvents[] = {
                    "Con_TileFlip",     "Con_Correct",    "Con_TileFlip_Incorrect",
                    "Con_NewScreen",    "Con_BoardCleared", "Con_Win",
                    "Con_GameOver",     "Con_PowerUp",    "Con_Mixer",
                    "Con_PurbleChef",   "Con_TimeDelay",  "Con_SneakPeek_Get",
                    "Con_SneakPeek_Click", "Con_SneakPeek_Starburst",
                };
                int loaded = 0;
                std::string names;
                for (const char* event : kEvents) {
                    PairSfx& sfx = pair_sfx(event);
                    if (sfx.ok) ++loaded;
                    if (sfx.ok) {
                        if (!names.empty()) names += ", ";
                        names += event;
                    }
                }
                const char* driver = SDL_GetCurrentAudioDriver();
                g_sfx_driver = driver == nullptr ? "(none)" : driver;
                std::printf("  audio %s: %d/%zu events decoded: %s\n", g_sfx_driver.c_str(), loaded,
                            sizeof(kEvents) / sizeof(kEvents[0]), names.c_str());
            } else {
                std::printf("  audio: disabled\n");
            }
        }
        if (keep_state) {
            // The board keeps its slot layout, deck, score, level and tick budget.
        } else {
            // The board needs the difficulty's whole <Levels> list: Board::NewLevel walks it, so
            // Intermediate plays two boards and Advanced four.
            std::vector<pple::ConcentrationLevel> chosen_levels;
            std::size_t chosen_at = 0;
            for (const pple::ConcentrationLevel& l : levels) {
                if (l.difficulty != chosen->difficulty) continue;
                if (l.level_index == chosen->level_index) chosen_at = chosen_levels.size();
                chosen_levels.push_back(l);
            }
            if (!pairs_board.reset(chosen_levels, chosen_at, pairs_pools, constants, pairs_box,
                                   seed_for_pairs)) {
                std::fprintf(stderr, "pairs: board reset failed\n");
                return;
            }
        }
        if (!keep_state && pairs_reveal) {
            // Reveal the whole deck, for inspecting the card art of every level.
            pairs_board.reveal_all();
        }
        // --pairs-solve clicks every matchable pair through the same handler, so the win state,
        // the score and the Professor's win movie can be reached without a mouse.  The rule is
        // Card::DoMatcher's: equal <id>, or either card is the Wild Card.  The wild is matched
        // last, because spending it on the first partner that happens to be available strands
        // the deck's single Time Bonus / Chef's cake cards.
        if (!keep_state && pairs_solve) {
            const int wild = pairs_pools.wild;
            const auto id_of = [&](int i) { return pairs_board.card_id(i); };
            const auto hidden = [&](int i) {
                return pairs_board.face(i) == pple::CardFace::Hidden;
            };
            const auto run_phase = [&](int phase) {
                bool progress = true;
                int guard = 0;
                while (progress && !pairs_board.finished() && guard++ < 128) {
                    progress = false;
                    for (int i = 0; i < pairs_board.cell_count() && !pairs_board.finished(); ++i) {
                        if (!hidden(i)) continue;
                        const int a = id_of(i);
                        for (int j = i + 1; j < pairs_board.cell_count(); ++j) {
                            if (!hidden(j)) continue;
                            const int b = id_of(j);
                            const bool wild_a = a == wild, wild_b = b == wild;
                            const bool take = phase == 0 ? (a == b && !wild_a && !wild_b)
                                             : phase == 1 ? (wild_a != wild_b)
                                                          : (wild_a && wild_b);
                            if (!take) continue;
                            if (!pairs_board.click(i).accepted) break;
                            const pple::ClickResult r = pairs_board.click(j);
                            if (!r.accepted) break;
                            std::printf("  solve click %d,%d -> %s, score %d, matches %d/%d\n", i, j,
                                        r.match ? "MATCH"
                                                : (r.wild_match ? "WILD (+10)"
                                                                : (r.mismatch ? "MISS" : "revealed")),
                                        pairs_board.score(), pairs_board.matches(),
                                        pairs_board.round_pairs_total());
                            // The special cards, so each one's effect is visible in the output
                            // rather than only in the code.
                            if (r.chef)
                                std::printf("    chef cake: swept every card with the Chef id\n");
                            if (r.mixer)
                                std::printf("    mixer: Board::DoMixer reshuffled the cards left\n");
                            if (r.sneak_get)
                                std::printf("    sneak icon: +%d (SneakMatchBonus)\n",
                                            pairs_constants.sneak_match_bonus);
                            if (r.time_bonus > 0)
                                std::printf("    time bonus: +%d ticks, %.0f s left\n", r.time_bonus,
                                            static_cast<double>(pairs_board.seconds_remaining()));
                            // Jar::StopExplosion runs at the end of the jar's burst, which this
                            // headless path has no frames for: end the board here so the solve
                            // walks every <Level> the way the engine does.
                            finish_board_end();
                            progress = true;
                            break;
                        }
                    }
                }
            };
            for (int phase = 0; phase < 3 && !pairs_board.finished(); ++phase) run_phase(phase);
            std::printf("  solve: %s, score %d, matches %d/%d\n",
                        pairs_board.won() ? "WON" : (pairs_board.lost() ? "LOST" : "unfinished"),
                        pairs_board.score(), pairs_board.matches(), pairs_board.round_pairs_total());
            if (!pairs_board.won()) {
                for (int i = 0; i < pairs_board.cell_count(); ++i) {
                    if (pairs_board.face(i) == pple::CardFace::Matched) continue;
                    std::printf("    left: cell %d id %d face %d (wild %d)\n", i, id_of(i),
                                static_cast<int>(pairs_board.face(i)), wild);
                }
                std::printf("    pending reveal %d, lockout %d\n", pairs_board.pending_reveal(),
                            pairs_board.lockout_ticks());
            }
        }
        // --pairs-ticks N advances the engine's 30 Hz step N times before the first frame, so the
        // time bar, the red threshold and the loss state can be inspected without waiting.
        if (!keep_state && pairs_ticks > 0) {
            pairs_board.tick(pairs_ticks);
            std::printf("  %d tick(s): %.0f s left, %s\n", pairs_ticks,
                        static_cast<double>(pairs_board.seconds_remaining()),
                        pairs_board.lost() ? "LOST" : (pairs_board.won() ? "WON" : "running"));
        }
        pairs_ready = true;
        pairs_end_played = false;
        // Board::InitUI (0x10008934D) plays Con_NewScreen (proto 0x71) whenever it builds a new
        // board, which is once per round and once per level change.
        if (!keep_state) play_pair_sfx("Con_NewScreen");
        // --pairs-click 3,7,12 flips those cells through the same handler the mouse uses, so a
        // scripted run exercises the reveal, the match, the scoring and the sounds.
        if (!keep_state && !pairs_click.empty()) {
            std::size_t at = 0;
            while (at < pairs_click.size()) {
                const std::size_t comma = pairs_click.find(',', at);
                const int cell = std::atoi(pairs_click.substr(at, comma - at).c_str());
                at = comma == std::string::npos ? pairs_click.size() : comma + 1;
                if (cell < 0 || cell >= pairs_board.cell_count()) {
                    std::printf("  click %d: out of range\n", cell);
                    continue;
                }
                if (pairs_board.face(cell) != pple::CardFace::Hidden) {
                    std::printf("  click %d: already face up\n", cell);
                    continue;
                }
                const pple::ClickResult r = pairs_board.click(cell);
                if (!r.accepted) {
                    std::printf("  click %d: refused (lockout %d)\n", cell, pairs_board.lockout_ticks());
                    continue;
                }
                std::printf("  click %-3d -> %s, score %d, matches %d/%d%s\n", cell,
                            r.match ? "MATCH" : (r.wild_match ? "WILD" : (r.mismatch ? "MISS" : "revealed")),
                            pairs_board.score(), pairs_board.matches(),
                            pairs_board.round_pairs_total(),
                            pairs_board.lockout_ticks() > 0 ? ", reveal timer armed" : "");
                play_click_sfx(r);
                // The scripted path has no frames to animate in, so the jar's flight and burst
                // are skipped and Jar::StopExplosion is called straight away.
                if (pairs_board.end_pending()) {
                    jar_begin_burst();
                    finish_board_end();
                }
                if (r.relaid_out) {
                    // The rest of the script refers to the old board, so stop here and let the
                    // main loop rebuild the art.
                    pairs_rebuild_pending = true;
                    pairs_keep_state = true;
                    break;
                }
            }
            // let the queued sounds actually reach the device before the capture or exit
            SDL_Delay(600);
        }
        if (loaded_scene) {
            for (const pple::SceneItem& item : loaded_scene->items) {
                if (item.name == "PlayArea" || item.instance == "PlayArea") {
                    // NodeBase::UpdateZero (0x10008CB8C) adds a child's own (x, y) to the parent's
                    // *world* origin and does not multiply it by the parent's scale, so the cards
                    // are at PlayArea + slot, with PlayArea's own 95% scaling only its own draw.
                    pairs_origin_x = item.x;
                    pairs_origin_y = item.y;
                    pairs_outer_scale = 1.0f;
                }
            }
        }
        // The card BACK is the CardBack of the current <Level>.  Card::InitUI (0x10007B8D8)
        // calls `Card::InitCardButton(this, *(DWORD *)(Board+0x40 + 328))` -- Board+0x40 is the
        // Level and Level+0x148 is its index -- and Card::InitCardButton (0x10007CB4C) builds
        // Card+0x98 ("CardButton") from `GetXml(*(CardBack[level] + 8))`, i.e. /buttonpath.
        // Level 0 is ShamButton.xml, so the shipped Beginner screen's backs are shamrocks.
        {
            const auto* cardbacks = bundle.find("UI\\Concentration\\CardBacks.xml");
            if (cardbacks != nullptr) {
                const std::vector<pple::CardBackPaths> backs =
                    pple::concentration_cardbacks(bundle.read(*cardbacks));
                const std::size_t at = static_cast<std::size_t>(pairs_board.level_position());
                if (at < backs.size() && !backs[at].button.empty()) pairs_back_xml = backs[at].button;
                std::printf("  card back: level %d -> %s\n", pairs_board.level_position(),
                            pairs_back_xml.c_str());
                // The jar's tray.  Jar::DoFiller (0x100083B10) picks its point file by
                // Round+0x124, the same level index:
                //   `want = points / NumCards * Round+0x12C`, then one coin per point up to
                //   `want`, each built from `GetXml(*(CardBack[level] + 264))` (/imagepath) with
                //   its scale forced to 38 percent.
                pairs_jar_coins.clear();
                pairs_jar_origin_x = 0.0f;
                pairs_jar_origin_y = 0.0f;
                float movie_container_x = 0.0f, movie_container_y = 0.0f;
                if (loaded_scene) {
                    for (const pple::SceneItem& item : loaded_scene->containers) {
                        const std::string& key = item.name.empty() ? item.instance : item.name;
                        if (key == "JarContainer") {
                            pairs_jar_origin_x = item.x;
                            pairs_jar_origin_y = item.y;
                        } else if (key == "JarMovieContainer") {
                            // Jar::LoadMovies hands /JarMovieContainer to
                            // PurblePlaceMovie::InitFromXml as the parts' parent.
                            movie_container_x = item.x;
                            movie_container_y = item.y;
                        }
                    }
                }
                // /JarFg is "CurJar": its own size is the jar the coins land in, and its x is
                // what `LidOffSet + CurJar.w/2` measures.
                pairs_curjar_w = 112.0f;
                pairs_curjar_h = 112.0f;
                if (loaded_scene) {
                    for (const pple::SceneItem& item : loaded_scene->items) {
                        const std::string& key = item.name.empty() ? item.instance : item.name;
                        if (key != "JarFg") continue;
                        float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
                        pple::sprite_frame_rect(item, 0, fx, fy, fw, fh);
                        if (fw > 0.0f) pairs_curjar_w = fw * item.scale;
                        if (fh > 0.0f) pairs_curjar_h = fh * item.scale;
                    }
                }
                // /ArcAnimation: the flight's length and the jar's lid offset.
                if (loaded_scene) {
                    pple::XmlNode root;
                    std::string xml_error;
                    if (pple::xml_parse(scene_bytes, root, xml_error)) {
                        const pple::XmlNode* arc = root.find("ArcAnimation");
                        if (arc != nullptr) {
                            const std::string frames_text = arc->child_text("Frames");
                            const int frames = frames_text.empty() ? 25 : std::atoi(frames_text.c_str());
                            if (frames > 0) pairs_arc_frames = frames;
                            const std::string lid_text = arc->child_text("LidOffSet");
                            pairs_arc_lid = static_cast<float>(
                                std::atof(lid_text.empty() ? "-15" : lid_text.c_str()));
                        }
                    }
                }
                static const char* kPointFiles[4] = {
                    "UI\\Concentration\\Shamrock_Points.xml", "UI\\Concentration\\Heart_Points.xml",
                    "UI\\Concentration\\Smiley_Points.xml", "UI\\Concentration\\GumDrop_Points.xml"};
                const std::size_t coin_at = std::min<std::size_t>(
                    static_cast<std::size_t>(pairs_board.level_position()), 3);
                const auto* points_slot = bundle.resolve(kPointFiles[coin_at], scene_how);
                std::vector<pple::JarPoint> points;
                if (points_slot != nullptr)
                    points = pple::concentration_jar_points(bundle.read(*points_slot));
                if (at < backs.size() && !backs[at].image.empty() && !points.empty()) {
                    const Sprite coin = resolve_sprite(bundle, backs[at].image, 0);
                    std::string coin_how;
                    const auto* coin_slot = coin.ok ? bundle.resolve(coin.requested, coin_how) : nullptr;
                    if (coin.ok && coin_slot != nullptr) {
                        SDL_Texture* tex = load_sheet_texture(coin.requested, coin_slot->entry->name,
                                                              pairs_sheet_note);
                        if (tex != nullptr) {
                            pple::SceneItem geo;
                            geo.src_x = coin.src.x;
                            geo.src_y = coin.src.y;
                            geo.src_w = coin.src.w;
                            geo.src_h = coin.src.h;
                            geo.frames = coin.frames;
                            geo.per_row = coin.per_row;
                            geo.prepped = coin.prepped;
                            float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
                            pple::sprite_frame_rect(geo, 0, fx, fy, fw, fh);
                            // The same sprite unscaled is the in-flight coin: ArcBehavior::Init
                            // (0x100083E48) sets its scale to Board+0x170 + 10 percent, and
                            // Board+0x170 is the card scale Board::ShuffleCards (0x1000896C4)
                            // computes, so the coin starts at the card's own size.
                            pairs_coin_tex = tex;
                            pairs_coin_src = SDL_FRect{fx, fy, fw, fh};
                            pairs_coin_w = fw;
                            pairs_coin_h = fh;
                            pairs_coin_reg_x = coin.reg_x;
                            pairs_coin_reg_y = coin.reg_y;
                            const float scale = 0.38f;  // Jar::DoFiller's `+ 6) = 38`
                            for (const pple::JarPoint& p : points) {
                                JarCoin c;
                                c.tex = tex;
                                c.src = SDL_FRect{fx, fy, fw, fh};
                                c.w = fw * scale;
                                c.h = fh * scale;
                                c.reg_x = coin.reg_x * scale;
                                c.reg_y = coin.reg_y * scale;
                                c.x = static_cast<float>(p.x);
                                c.y = static_cast<float>(p.y);
                                c.rotation = p.rotation;
                                pairs_jar_coins.push_back(c);
                            }
                            std::printf("  jar: %s, %zu coin slot(s), coin %s at 38%%\n",
                                        kPointFiles[coin_at], pairs_jar_coins.size(),
                                        backs[at].image.c_str());
                        }
                    }
                }
                // Jar::LoadMovies (0x1000835D0): `(Board+0x20)[i] + 1032` for i = 0..3, so the
                // burst movie is per level.  Jar+0x30 selects `Jar+0x48[Round+0x124]`.  The
                // document itself is loaded lazily, when the burst starts.
                pairs_burst_name = (at < backs.size()) ? backs[at].movie : std::string{};
                pairs_burst_anchor_x = pairs_jar_origin_x + movie_container_x;
                pairs_burst_anchor_y = pairs_jar_origin_y + movie_container_y;
            }
        }
        const float card_scale = static_cast<float>(pairs_board.slots().front().scale_percent) / 100.0f;
        int failed = 0;
        card_cache.clear();
        // The jar starts each level empty: Jar::InitUI (0x100083218, at 0x100083593) zeroes
        // Jar+0xC8 and the burst's own wipe has already hidden the previous level's coins.
        pairs_arcs.clear();
        pairs_jar_fill = 0;
        pairs_jar_phase = JarPhase::Idle;
        pairs_burst_frame = 0.0f;
        card_flip.assign(pairs_board.cell_count(), CardFlip{});
        card_face_shown.assign(pairs_board.cell_count(), pple::CardFace::Hidden);
        const auto build_card_sprite = [&](const std::string& xml, SDL_Texture** tex_out,
                                           SDL_FRect& src_out, float& fw_out, float& fh_out,
                                           int* frames_out = nullptr) {
            const Sprite sprite = resolve_sprite(bundle, xml, 0);
            if (!sprite.ok) return false;
            if (frames_out != nullptr) *frames_out = sprite.frames;
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
        pairs_scene_draws = scene_draws.size();
        // The progress meter.  Board::Init (0x1000888C4) copies the first four
        // UI\Concentration\CardBacks.xml <Back> entries' /progpath into CardBack+0x508, and
        // ProgressMeter::InitUI (0x100084720) makes one indicator per <Level> of the difficulty
        // from `*(CardBack[i] + 1288)`, stacks them `(node->x, i * /ProgressMeterIndicators/yOffSet)`
        // apart under /ProgressMeterContainer, and then lights indicator i while
        // `i < Round+292` -- ConRoundData's `Level` field (ConRoundData::ToXml appends this[73]
        // as "Level").  The lit state is the node's frame: ProgressShamrock.xml declares
        // <FrameCount>2</FrameCount>, frame 0 is the hollow outline and frame 1 the filled art.
        pairs_meter.clear();
        pairs_tokens.clear();
        if (loaded_scene) {
            float mx = 0.0f, my = 0.0f, sx = 0.0f, sy = 0.0f;
            bool have_meter = false, have_sneak = false;
            for (const pple::SceneItem& item : loaded_scene->containers) {
                const std::string& key = item.name.empty() ? item.instance : item.name;
                if (key == "ProgressMeterContainer") {
                    mx = item.x;
                    my = item.y;
                    have_meter = true;
                } else if (key == "SneakPeekContainer") {
                    sx = item.x;
                    sy = item.y;
                    have_sneak = true;
                }
            }
            int y_offset = 55;
            if (const auto* scene_slot2 = bundle.resolve(scene_xml, scene_how)) {
                pple::XmlNode scene_root;
                std::string scene_error;
                if (pple::xml_parse(bundle.read(*scene_slot2), scene_root, scene_error)) {
                    std::vector<const pple::XmlNode*> stack{&scene_root};
                    while (!stack.empty()) {
                        const pple::XmlNode* node = stack.back();
                        stack.pop_back();
                        if (node->name == "ProgressMeterIndicators") {
                            const std::string text = node->child_text("yOffSet");
                            if (!text.empty()) y_offset = std::atoi(text.c_str());
                        }
                        for (const pple::XmlNode& child : node->children) stack.push_back(&child);
                    }
                }
            }
            if (have_meter) {
                const auto* cardbacks = bundle.find("UI\\Concentration\\CardBacks.xml");
                if (cardbacks != nullptr) {
                    const std::vector<pple::CardBackPaths> backs =
                        pple::concentration_cardbacks(bundle.read(*cardbacks));
                    const int count = std::min<int>(pairs_board.level_count(),
                                                    static_cast<int>(backs.size()));
                    for (int i = 0; i < count; ++i) {
                        const std::string& progpath = backs[static_cast<std::size_t>(i)].prog;
                        if (progpath.empty()) continue;
                        const Sprite sp = resolve_sprite(bundle, progpath, 0);
                        if (!sp.ok || sp.frames < 2) continue;
                        std::string sheet_how;
                        const auto* slot = bundle.resolve(sp.requested, sheet_how);
                        if (slot == nullptr) continue;
                        SDL_Texture* tex = load_sheet_texture(sp.requested, slot->entry->name, pairs_sheet_note);
                        if (tex == nullptr) continue;
                        pple::SceneItem geo;
                        geo.src_x = sp.src.x;
                        geo.src_y = sp.src.y;
                        geo.src_w = sp.src.w;
                        geo.src_h = sp.src.h;
                        geo.frames = sp.frames;
                        geo.per_row = sp.per_row;
                        geo.prepped = sp.prepped;
                        MeterDraw m;
                        m.tex = tex;
                        float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
                        pple::sprite_frame_rect(geo, 0, fx, fy, fw, fh);
                        m.src_unlit = SDL_FRect{fx, fy, fw, fh};
                        pple::sprite_frame_rect(geo, 1, fx, fy, fw, fh);
                        m.src_lit = SDL_FRect{fx, fy, fw, fh};
                        m.dst = SDL_FRect{mx - sp.reg_x, my + static_cast<float>(i * y_offset) - sp.reg_y,
                                          fw, fh};
                        pairs_meter.push_back(m);
                    }
                    std::printf("  progress meter: %zu indicator(s) at %.0f,%.0f step %d\n",
                                pairs_meter.size(), static_cast<double>(mx), static_cast<double>(my),
                                y_offset);
                }
            }
            if (have_sneak) {
                // SneakPeakArea::InitUI -> ConUILoader::LoadSneakPeekToken (0x10008C48C): the
                // shipped <SneakPeekToken> entries carry only <Sprite>, so /x and /y default to 0
                // and every token lands on the container's own origin; the count shown is
                // Round+0x11C (Tokens) via `index + 1 > Round.Tokens -> hide`.
                const Sprite sp = resolve_sprite(bundle, "UI\\Concentration\\SneakPeakButton.xml", 0);
                if (sp.ok) {
                    const SDL_FRect rect{sx - sp.reg_x, sy - sp.reg_y, sp.src.w, sp.src.h};
                    for (int i = 0; i < 3; ++i) pairs_tokens.push_back(rect);
                    std::printf("  sneak tokens: %d at %.0f,%.0f (%.0f x %.0f)\n", pairs_board.tokens(),
                                static_cast<double>(rect.x), static_cast<double>(rect.y),
                                static_cast<double>(rect.w), static_cast<double>(rect.h));
                }
            }
        }
        // Clickable elements of the game screen.  ReturnToMainMenu is a real node; the MCE
        // New Game / Exit Game pair are drawn only with --mce (see ConcentrationGame::LoadGameUI
        // 0x10008122B).  Their rect is the engine's: (x, y) - registration, sized SourceWidth x
        // SourceHeight.
        conc_hotspots.clear();
        if (loaded_scene) {
            for (const pple::SceneItem& item : loaded_scene->items) {
                const std::string& key = item.name.empty() ? item.instance : item.name;
                const bool wanted = key == "ReturnToMainMenu" ||
                                    (mce_mode && (key == "NewGame" || key == "ExitGame"));
                if (!wanted || item.sheet.empty()) continue;
                Hotspot spot;
                spot.name = key;
                spot.rect = SDL_FRect{(item.x - item.reg_x * item.scale) * item.scale,
                                      (item.y - item.reg_y * item.scale) * item.scale,
                                      item.src_w * item.scale, item.src_h * item.scale};
                conc_hotspots.push_back(spot);
            }
        }
        // The two MCE header buttons sit at y=-51/-41 in the document, i.e. above the scene
        // origin; they only land on screen if the host draws the scene 51 px down inside a
        // header strip, which is what --mce reproduces.
        if (mce_mode) {
            for (Hotspot& spot : conc_hotspots)
                if (spot.name == "NewGame" || spot.name == "ExitGame") spot.rect.y += 51.0f;
        }
        pairs_title = scene_xml + "   [Purble Pairs] " + chosen->difficulty + " #" +
                      std::to_string(chosen->level_index);
        std::printf("pairs %s #%d: %d cells, cell %dx%d scale %d%%, %s\n",                    chosen->difficulty.c_str(), chosen->level_index, pairs_board.cell_count(),                    pairs_board.slots().front().w, pairs_board.slots().front().h,                    pairs_board.slots().front().scale_percent,                    (timebar.ok ? (std::to_string(timebar.segments) + " time bar segments")                                : std::string("untimed")).c_str());
        if (failed != 0) std::printf("  %d card sprite(s) did not resolve\n", failed);
        refresh_hud();
        timed_refresh();
        const Uint64 build_t1 = SDL_GetPerformanceCounter();
        build_ms += 1000.0 * static_cast<double>(build_t1 - build_t0) /
                    static_cast<double>(SDL_GetPerformanceFrequency());
        ++build_calls;
    };
    // The card layer: only the face choice, the highlight and the flip progress change after a
    // build, so this is all that runs per tick.  No XML, no sheet loads.
    // Assigned after build_pairs; the Professor is a movie the scene document does not place.
    load_professor = [&](const std::string& instance) {
        if (!loaded_scene) {
            std::printf("  professor %s: no scene\n", instance.c_str());
            return false;
        }
        for (const pple::SceneItem& item : loaded_scene->items) {
            if (item.instance != instance && item.name != instance) continue;
            std::string m_how;
            const auto* m_slot = bundle.resolve(item.ref, m_how);
            if (m_slot == nullptr) {
                std::printf("  professor %s: movie resource not found: %s\n", instance.c_str(),
                            item.ref.c_str());
                return false;
            }
            pple::Movie movie;
            std::string m_error;
            if (!pple::movie_load(bundle.read(*m_slot), fetch_resource, movie, m_error)) {
                std::printf("  professor %s: %s\n", instance.c_str(), m_error.c_str());
                return false;
            }
            professor.movie = std::move(movie);
            professor.which = instance;
            professor.ok = true;
            professor.time = 0.0f;
            return true;
        }
        std::printf("  professor %s: no such movie slot in the scene\n", instance.c_str());
        return false;
    };
    refresh_cards = [&]() {
        if (!pairs_ready) return;
        scene_draws.resize(std::min(scene_draws.size(), pairs_scene_draws));
        float extent_w = scene_w, extent_h = scene_h;
        for (std::size_t i = 0; i < card_cache.size(); ++i) {
            CardCache& entry = card_cache[i];
            const bool is_highlight = (i + 1 == card_cache.size());
            if (is_highlight) {
                const int pending = pairs_board.pending_reveal();
                if (pending < 0 || pending >= pairs_board.cell_count()) continue;
                if (entry.tex_face == nullptr) continue;
                const pple::ConcentrationSlot& slot = pairs_board.slots()[pending];
                const CardFlip& f = card_flip[pending];
                const float sx = static_cast<float>(f.scale_x) / 100.0f;
                const float sy = static_cast<float>(f.scale_y) / 100.0f;
                const float base_x = pairs_origin_x + static_cast<float>(slot.x);
                const float base_y = pairs_origin_y + static_cast<float>(slot.y);
                DrawItem d;
                d.texture = entry.tex_face;
                d.src = entry.src_face;
                d.dst = SDL_FRect{
                    base_x + static_cast<float>((100.0f - static_cast<float>(f.scale_x)) / 100.0f *
                                 static_cast<float>(slot.w) * 0.5f),
                    base_y + static_cast<float>(fabs(100.0f - static_cast<float>(f.scale_y)) / 100.0f *
                                 static_cast<float>(slot.h) * -1.0f),
                    entry.w * sx, entry.h * sy};
                scene_draws.push_back(d);
                extent_w = std::max(extent_w, d.dst.x + d.dst.w);
                extent_h = std::max(extent_h, d.dst.y + d.dst.h);
                continue;
            }
            if (entry.index < 0 || entry.index >= pairs_board.cell_count()) continue;
            const pple::CardFace face = pairs_board.face(entry.index);
            const CardFlip& f = card_flip[entry.index];
            // Which side is on screen: mid-flip it is the side being collapsed, otherwise the
            // board's own face state decides.
            bool show_face = face != pple::CardFace::Hidden;
            if (f.phase == FlipPhase::Collapse) show_face = !f.to_face;
            else if (f.phase == FlipPhase::Expand) show_face = f.to_face;
            SDL_Texture* tex = show_face ? entry.tex_face : entry.tex_back;
            const SDL_FRect& src = show_face ? entry.src_face : entry.src_back;
            if (tex == nullptr) continue;
            // The engine recentres the narrowing card on the *cell*, not on the sprite:
            // SetPosition(node, fabs(100 - xscale)/100 * card.w * 0.5, ... * card.h * -1.0).
            const float off_x = (100.0f - static_cast<float>(f.scale_x)) / 100.0f * entry.cell_w * 0.5f;
            const float off_y = fabs(100.0f - static_cast<float>(f.scale_y)) / 100.0f *
                                entry.cell_h * -1.0f;
            DrawItem d;
            d.texture = tex;
            d.src = src;
            d.dst = SDL_FRect{entry.base_x + off_x, entry.base_y + off_y,
                              entry.w * static_cast<float>(f.scale_x) / 100.0f,
                              entry.h * static_cast<float>(f.scale_y) / 100.0f};
            d.alpha = 255;
            scene_draws.push_back(d);
            extent_w = std::max(extent_w, d.dst.x + d.dst.w);
            extent_h = std::max(extent_h, d.dst.y + d.dst.h);
        }
        // The progress meter: indicator i shows its filled frame while
        // `i < Round+0x124`, which is ConRoundData's Level index -- the number of levels of this
        // difficulty already cleared.
        for (std::size_t i = 0; i < pairs_meter.size(); ++i) {
            const MeterDraw& m = pairs_meter[i];
            if (m.tex == nullptr) continue;
            const bool lit = static_cast<int>(i) < pairs_board.level_position();
            DrawItem d;
            d.texture = m.tex;
            d.src = lit ? m.src_lit : m.src_unlit;
            d.dst = m.dst;
            d.alpha = 255;
            scene_draws.push_back(d);
            extent_w = std::max(extent_w, d.dst.x + d.dst.w);
            extent_h = std::max(extent_h, d.dst.y + d.dst.h);
        }
        // The jar's flying coins.  ArcBehavior::Update (0x100084128) walks a quadratic Bezier
        // from the source through the apex to the destination with `(1-t)^2`, `2(1-t)t`, `t^2`,
        // `t = tick / /ArcAnimation/Frames`, and interpolates the node's scale over the same
        // curve from `s` to `2s` to `s/2` (ArcBehavior::Init's three Array adds).  The alpha
        // (`node+0x24`) grows by 0.2 per tick.
        // They are children of the "ArcContainer" Base, which Jar::InitUI creates *before* the
        // Base that holds the tray coins, so they draw behind the tray and in front of /JarBkg.
        std::vector<DrawItem> jar_layer;
        for (const ArcCoin& a : pairs_arcs) {
            if (a.tex == nullptr) continue;
            double t = static_cast<double>(a.tick) / static_cast<double>(pairs_arc_frames);
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            const double u = 1.0 - t;
            const double w0 = u * u, w1 = 2.0 * u * t, w2 = t * t;
            const float px = static_cast<float>(w0 * a.sx + w1 * a.cx + w2 * a.dx);
            const float py = static_cast<float>(w0 * a.sy + w1 * a.cy + w2 * a.dy);
            const float scale =
                static_cast<float>(w0 * a.scale0 + w1 * (2.0 * a.scale0) + w2 * (a.scale0 * 0.5)) /
                100.0f;
            DrawItem d;
            d.texture = a.tex;
            d.src = a.src;
            d.dst = SDL_FRect{px - a.reg_x * scale, py - a.reg_y * scale, a.sprite_w * scale,
                              a.sprite_h * scale};
            d.alpha = static_cast<int>(std::clamp(0.2f * static_cast<float>(a.tick), 0.0f, 1.0f) *
                                       255.0f);
            jar_layer.push_back(d);
            extent_w = std::max(extent_w, d.dst.x + d.dst.w);
            extent_h = std::max(extent_h, d.dst.y + d.dst.h);
        }
        // The jar's tray.  Jar::DoFiller (0x100083B10):
        //   want = (int)(points / Board+0x28 * Round+0x12C)
        // Board+0x28 and Round+0x12C are both the *current* level's numbers, so the fill is that
        // level's fraction and Jar+0xC8 restarts at each level.  `pairs_jar_fill` is grown when a
        // coin lands, which is when the engine calls DoFiller (ArcBehavior::Update's tail), not
        // when the match is made.  The coins go between /JarBkg and /JarFg.
        const BurstMovie* burst =
            pairs_jar_phase == JarPhase::Burst ? load_burst(pairs_burst_name) : nullptr;
        if (burst != nullptr) {
            // Jar::Update's wipe: as the burst runs the tray's top coins are hidden, from
            // `v17 = n - 1 - frame * ((n + 2) / frameCount)` upwards, and nothing ever shows them
            // again -- the burst movie draws the coins erupting itself.
            const double n = static_cast<double>(pairs_jar_fill);
            const double v17 = n - 1.0 - static_cast<double>(pairs_burst_frame) *
                                           ((n + 2.0) /
                                            std::max(1.0, static_cast<double>(
                                                              burst->movie.frame_count)));
            const int left = v17 > 0.0 ? static_cast<int>(std::ceil(v17)) : 0;
            if (left < pairs_jar_fill) pairs_jar_fill = left;
        }
        for (int i = 0; i < pairs_jar_fill && i < static_cast<int>(pairs_jar_coins.size()); ++i) {
            const JarCoin& c = pairs_jar_coins[static_cast<std::size_t>(i)];
            if (c.tex == nullptr) continue;
            DrawItem d;
            d.texture = c.tex;
            d.src = c.src;
            // NodeSprite::UpdateZero: the drawn corner is (x, y) - scale * registration / 100.
            d.dst = SDL_FRect{pairs_jar_origin_x + c.x - c.reg_x,
                              pairs_jar_origin_y + c.y - c.reg_y, c.w, c.h};
            d.alpha = 255;
            d.rotation = c.rotation;
            jar_layer.push_back(d);
        }
        // The jar's burst movie, in the same layer: Jar+0x30 is the current level's /moviepath
        // and Jar::Update shows it once the last coin has landed.
        if (burst != nullptr) {
            append_movie(burst->movie, static_cast<int>(pairs_burst_frame), pairs_burst_anchor_x,
                         pairs_burst_anchor_y, jar_layer, extent_w, extent_h);
        }
        if (!jar_layer.empty() && pairs_jar_front_at >= 0) {
            const std::size_t at = static_cast<std::size_t>(std::min<int>(
                pairs_jar_front_at, static_cast<int>(scene_draws.size())));
            scene_draws.insert(scene_draws.begin() + static_cast<std::ptrdiff_t>(at),
                               jar_layer.begin(), jar_layer.end());
        }
        // The time bar: segment i is lit while i >= elapsed, red for the last 900 ticks.
        const int pending = pairs_board.pending_reveal();
        (void)pending;
        if (timebar.ok) {
            const int total = pairs_board.level().total_ticks();
            const int left = static_cast<int>(pairs_board.ticks_remaining());
            int elapsed = 0;
            if (timebar.segments > 0 && total > 0) {
                const double per = static_cast<double>(total) / static_cast<double>(timebar.segments);
                if (per > 0.0) elapsed = static_cast<int>((total - left) / per);
            }
            if (elapsed < 0) elapsed = 0;
            if (elapsed > timebar.segments) elapsed = timebar.segments;
            const bool red = left < 900;
            SDL_Texture* tex = red ? timebar.red : timebar.green;
            const SDL_FRect* src = red ? &timebar.red_src : &timebar.green_src;
            const float x = red ? timebar.x_red : timebar.x_green;
            if (tex != nullptr) {
                for (int i = elapsed; i < timebar.segments; ++i) {
                    DrawItem d;
                    d.texture = tex;
                    d.src = *src;
                    d.dst = SDL_FRect{x, timebar.y_top + static_cast<float>(i),
                                      src->w, src->h};
                    d.alpha = 255;
                    scene_draws.push_back(d);
                }
            }
        }
        scene_w = extent_w;
        scene_h = extent_h;
        // The Professor: idle while the round runs, win or lose once it is over.  The engine's
        // reaction is a movie swap (OnEnterWonGameState 0x1000821D0 / OnEnterLoseGameState
        // 0x10008259C), so the same thing happens here.
        if (professor.ok) {
            const char* want = pairs_board.round_won()
                                   ? "ProfessorWinAnimation"
                                   : (pairs_board.round_over() ? "ProfessorLoseAnimation" : nullptr);
            if (want != nullptr && professor.which != want) {
                if (load_professor(want)) std::printf("  professor -> %s\n", want);
            }
            std::vector<pple::MoviePlacement> placements;
            const float fps =
                professor.movie.parts.empty()
                    ? 12.0f
                    : pple::movie_frame_rate(professor.movie, professor.movie.parts.front());
            const int mframe =
                (professor.movie.frame_count > 1 && fps > 0.0f)
                    ? static_cast<int>(professor.time * fps) % professor.movie.frame_count
                    : 0;
            pple::movie_evaluate(professor.movie, mframe, placements);
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
                d.dst = SDL_FRect{professor.anchor_x + p.x - p.part->sprite.reg_x,
                                  professor.anchor_y + p.y - p.part->sprite.reg_y,
                                  sw * p.xscale, sh * p.yscale};
                d.alpha = static_cast<int>(std::clamp(p.alpha * 255.0f, 0.0f, 255.0f));
                d.rotation = p.rotation;
                scene_draws.push_back(d);
                extent_w = std::max(extent_w, d.dst.x + d.dst.w);
                extent_h = std::max(extent_h, d.dst.y + d.dst.h);
            }
            scene_w = std::max(scene_w, extent_w);
            scene_h = std::max(scene_h, extent_h);
        }
    };
    // ---- card flip animation ---------------------------------------------------------
    // Card::FlipForward (0x10007CFA4) does not change the art: it sets the state to 3 and lets
    // Card::UpdateFlipForward (0x10007EA20) animate, and Card::OnReveal (0x10007F824) only runs
    // when the flip finishes.  The same shape here: a face change starts a flip, and the drawn
    // side follows the phase.
    const auto start_flip = [&](int index, bool to_face) {
        if (index < 0 || index >= static_cast<int>(card_flip.size())) return;
        CardFlip& f = card_flip[static_cast<std::size_t>(index)];
        if (f.phase != FlipPhase::None) return;
        f.phase = FlipPhase::Collapse;
        f.scale_x = 100;
        f.scale_y = 100;
        f.to_face = to_face;
    };
    const auto sync_flips = [&]() {
        for (std::size_t i = 0; i < card_face_shown.size(); ++i) {
            const pple::CardFace now = pairs_board.face(static_cast<int>(i));
            if (now == card_face_shown[i]) continue;
            const bool was_hidden = card_face_shown[i] == pple::CardFace::Hidden;
            const bool is_hidden = now == pple::CardFace::Hidden;
            // A Hidden <-> shown change is a flip; Shown -> Matched is not, the card simply
            // stays up.
            if (was_hidden != is_hidden) start_flip(static_cast<int>(i), !is_hidden);
            card_face_shown[i] = now;
        }
    };
    const auto advance_flips = [&]() {
        bool changed = false;
        for (CardFlip& f : card_flip) {
            if (f.phase == FlipPhase::Collapse) {
                f.scale_x -= 17;  // the visible side narrows at 17 percent per tick ...
                f.scale_y += 2;   // ... and drifts 2 taller
                if (f.scale_x <= 15) {  // the engine clamps the collapse at 15 percent
                    f.scale_x = 15;
                    f.phase = FlipPhase::Expand;
                }
                changed = true;
            } else if (f.phase == FlipPhase::Expand) {
                f.scale_x += 17;
                f.scale_y -= 2;
                if (f.scale_x >= 100) {
                    f.scale_x = 100;
                    f.scale_y = 100;
                    f.phase = FlipPhase::None;
                }
                changed = true;
            }
        }
        return changed;
    };
    // --bench measures the card layer: refresh_cards is what replaced the per-tick build_pairs,
    // so its cost per call is the number that matters.
    timed_refresh = [&]() {
        const Uint64 t0 = SDL_GetPerformanceCounter();
        refresh_cards();
        const Uint64 t1 = SDL_GetPerformanceCounter();
        refresh_total_ms += 1000.0 * static_cast<double>(t1 - t0) /
                            static_cast<double>(SDL_GetPerformanceFrequency());
        ++refresh_calls;
    };
    // ---- the main menu ---------------------------------------------------------------
    // PurblePlaceManager::Init (0x10007091C) walks UI\PurblePlace\PurblePlaceManager.xml by
    // @Name: <Background> (whose *text* is a sprite name, created at 0,0 -- LoadBackground
    // 0x100072DE4), <NodeSprite Name="Foreground">, two title labels, four <NodeButton>s and two
    // <Loading> labels that are then hidden.  Slot 0 of every button is transparent by design
    // (section 10.5): the icons are painted into PurblePlaceForeground.png and the buttons are
    // hot-spots over them, with slots 1 and 2 as the hover and pressed art.
    const std::string menu_xml = "UI\\PurblePlace\\PurblePlaceManager.xml";
    const auto build_menu = [&]() {
        release_draws();
        scene_w = scene_h = 0.0f;
        menu_hotspots.clear();
        std::string how;
        const auto* slot = bundle.resolve(menu_xml, how);
        if (slot == nullptr) {
            std::fprintf(stderr, "menu not found: %s\n", menu_xml.c_str());
            return;
        }
        const auto fetch = [&](const std::string& name, std::vector<std::uint8_t>& out) {
            std::string inner_how;
            const auto* s = bundle.resolve(name, inner_how);
            if (s == nullptr) return false;
            out = bundle.read(*s);
            return true;
        };
        pple::Scene scene;
        std::string error;
        if (!pple::scene_load(bundle.read(*slot), fetch, scene, error)) {
            std::fprintf(stderr, "menu parse: %s\n", error.c_str());
            return;
        }
        std::size_t drawn = 0;
        for (const pple::SceneItem& item : scene.items) {
            const std::string& key = item.name.empty() ? item.instance : item.name;
            if (item.kind == pple::NodeKind::Label) {
                // The two "Loading" labels are hidden once the menu is up (0x100070EEA / 0x100070F36).
                if (key == "Loading" || item.text.empty()) continue;
                TTF_Font* f = font_for(item.font_size > 0 ? item.font_size : 16.0f);
                if (f == nullptr) continue;
                SDL_Surface* surface = TTF_RenderText_Blended(
                    f, item.text.c_str(), item.text.size(),
                    SDL_Color{static_cast<Uint8>((item.colour >> 16) & 0xFF),
                              static_cast<Uint8>((item.colour >> 8) & 0xFF),
                              static_cast<Uint8>(item.colour & 0xFF),
                              static_cast<Uint8>((item.colour >> 24) & 0xFF)});
                if (surface == nullptr) continue;
                const float box_w = item.w > 0 ? item.w * item.scale : 0.0f;
                const float box_h = item.h > 0 ? item.h * item.scale : 0.0f;
                float dx = item.x * item.scale;
                if ((item.justify & 0x04) != 0) {
                    if (box_w > 0.0f) dx += box_w - static_cast<float>(surface->w);
                } else if ((item.justify & 0x40) != 0) {
                    if (box_w > 0.0f) dx += (box_w - static_cast<float>(surface->w)) * 0.5f;
                }
                float dy = item.y * item.scale;
                if ((item.justify & 0x01) != 0) {
                    if (box_h > 0.0f) dy += box_h - static_cast<float>(surface->h);
                } else if ((item.justify & 0x20) != 0) {
                    if (box_h > 0.0f) dy += (box_h - static_cast<float>(surface->h)) * 0.5f;
                }
                DrawItem d;
                d.texture = SDL_CreateTextureFromSurface(renderer, surface);
                d.dst = SDL_FRect{dx, dy, static_cast<float>(surface->w),
                                  static_cast<float>(surface->h)};
                d.alpha = item.alpha;
                SDL_DestroySurface(surface);
                scene_draws.push_back(d);
                ++drawn;
                continue;
            }
            if (item.kind != pple::NodeKind::Sprite && item.kind != pple::NodeKind::Button) {
                continue;
            }
            if (item.sheet.empty()) continue;
            // The engine starts a button at animation 0 and moves to 1 on <enter> and 2 on <down>
            // (NodeButton::HandleEvent 0x1000AAC92 / 0x1000AAE1C).
            const bool hovered = item.kind == pple::NodeKind::Button && key == menu_hover_name;
            const int state = hovered ? 1 : 0;
            const Sprite sprite = resolve_sprite(bundle, item.ref, state);
            if (!sprite.ok) continue;
            std::string sheet_how;
            const auto* sheet_slot = bundle.resolve(sprite.requested, sheet_how);
            if (sheet_slot == nullptr) continue;
            std::string note;
            SDL_Texture* tex = load_sheet_texture(sprite.requested, sheet_slot->entry->name, note);
            if (tex == nullptr) continue;
            float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
            pple::SceneItem geometry;
            geometry.src_x = sprite.src.x;
            geometry.src_y = sprite.src.y;
            geometry.src_w = sprite.src.w;
            geometry.src_h = sprite.src.h;
            geometry.frames = sprite.frames;
            geometry.per_row = sprite.per_row;
            geometry.prepped = sprite.prepped;
            pple::sprite_frame_rect(geometry, 0, fx, fy, fw, fh);
            // NodeSprite::UpdateZero (0x100093740) subtracts the scaled registration offset.
            const float dx = (item.x - sprite.reg_x * item.scale) * item.scale;
            const float dy = (item.y - sprite.reg_y * item.scale) * item.scale;
            DrawItem d;
            d.texture = tex;
            d.src = SDL_FRect{fx, fy, fw, fh};
            d.dst = SDL_FRect{dx, dy, fw * item.scale, fh * item.scale};
            d.alpha = item.alpha;
            scene_draws.push_back(d);
            scene_w = std::max(scene_w, d.dst.x + d.dst.w);
            scene_h = std::max(scene_h, d.dst.y + d.dst.h);
            ++drawn;
            if (item.kind == pple::NodeKind::Button) {
                Hotspot spot;
                spot.name = key;
                spot.rect = SDL_FRect{dx, dy, fw * item.scale, fh * item.scale};
                menu_hotspots.push_back(spot);
            }
        }
        scene_w = std::max(scene_w, pple::kScreenWidth);
        scene_h = std::max(scene_h, pple::kScreenHeight);
        menu_ready = true;
        std::printf("menu %s: %zu drawn, %zu hot-spot(s)\n", menu_xml.c_str(), drawn,
                    menu_hotspots.size());
        for (const Hotspot& spot : menu_hotspots)
            std::printf("  hot-spot %-20s %.0f,%.0f %.0fx%.0f\n", spot.name.c_str(),
                        static_cast<double>(spot.rect.x), static_cast<double>(spot.rect.y),
                        static_cast<double>(spot.rect.w), static_cast<double>(spot.rect.h));
        hud_lines = {menu_xml, std::to_string(drawn) + " items drawn",
                     menu_hover_name.empty() ? std::string("click a building") : menu_hover_name};
        rebuild_hud();
    };
    const auto load_current = [&](bool announce) {
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
    };
    // --menu needs the Concentration document as well, because its Purble Pairs hot-spot enters
    // the game: the menu is the entry point, not a separate mode.
    if (menu_mode && scene_xml.empty())
        scene_xml = "UI\\CONCENTRATION\\CONCENTRATIONGAME.XML";
    if (!pairs_spec.empty()) {
        screen = Screen::Concentration;
        load_current(true);
    } else if (menu_mode) {
        screen = Screen::Menu;
        build_menu();
    } else {
        load_current(true);
    }
    // Non-interactive 1:1 dump, so decoded pixels can be analysed outside the viewer.
    if (!save_path.empty() && sprite_xml.empty() && scene_xml.empty() && movie_xml.empty() && pairs_spec.empty()) {
        const bool ok = current.ok && write_ppm(save_path, current);
        std::printf("%s: %s (%dx%d)\n", save_path.c_str(), ok ? "written" : "failed", current.width,                    current.height);
        for (SDL_Texture* t : hud_textures) SDL_DestroyTexture(t);
        if (texture != nullptr) SDL_DestroyTexture(texture);
        for (const auto& [cache_name, t] : sheet_cache) SDL_DestroyTexture(t);
        for (auto& [size, f] : font_cache)            if (f != nullptr) TTF_CloseFont(f);
        if (font != nullptr) TTF_CloseFont(font);
        TTF_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ok ? 0 : 1;
    }
    // One click handler for the mouse and for --script-click, so a scripted run exercises the
    // same navigation a mouse does.  Coordinates are scene coordinates.  Returns false when the
    // window should close.
    const auto click_scene = [&](float sx, float sy, bool motion_only) -> bool {
        if (screen == Screen::Menu && menu_ready) {
            const Hotspot* hit = nullptr;
            for (const Hotspot& spot : menu_hotspots) {
                const SDL_FRect& r = spot.rect;
                if (sx >= r.x && sx < r.x + r.w && sy >= r.y && sy < r.y + r.h) {
                    hit = &spot;
                    break;
                }
            }
            const std::string want = hit == nullptr ? std::string{} : hit->name;
            if (want != menu_hover_name) {
                menu_hover_name = want;
                build_menu();  // slot 1 is the hover art; only rebuild when the target changes
            }
            if (motion_only || hit == nullptr) return true;
            if (hit->name == "ConcentrationButton") {
                std::printf("menu: ConcentrationButton -> Purble Pairs\n");
                screen = Screen::Concentration;
                menu_ready = false;
                pairs_ready = false;
                build_pairs();
            } else if (hit->name == "ExitButton") {
                std::printf("menu: ExitButton clicked - exiting, as the game does\n");
                return false;
            } else {
                std::printf("menu: %s is not implemented in this reconstruction "
                            "(only Purble Pairs is)\n", hit->name.c_str());
            }
            return true;
        }
        if (!pairs_ready) return true;
        // Window -> scene -> card.  The target is the card's own node: the drawn sprite rect at
        // the cell's top-left, which at Beginner is 88x90 over a 74x74 cell, so the visible
        // card's right and bottom edges do work and neighbouring cells overlap by a few pixels.
        // The engine's node hit test takes the topmost node, so the scan runs index-descending.
        for (const Hotspot& spot : conc_hotspots) {
            const SDL_FRect& r = spot.rect;
            if (sx < r.x || sx >= r.x + r.w || sy < r.y || sy >= r.y + r.h) continue;
            if (spot.name == "ReturnToMainMenu") {
                std::printf("pairs: ReturnToMainMenu -> main menu\n");
                // The engine saves statistics and game state here (SaveGameStats 0x100082C70 /
                // SaveGameState 0x100082B40) and then sets state 4; this reconstruction does not
                // write save files.
                pairs_ready = false;
                screen = Screen::Menu;
                menu_hover_name.clear();
                build_menu();
            } else if (spot.name == "NewGame") {
                std::printf("pairs: NewGame -> new round\n");
                build_pairs();
            } else if (spot.name == "ExitGame") {
                std::printf("pairs: ExitGame -> main menu\n");
                pairs_ready = false;
                screen = Screen::Menu;
                menu_hover_name.clear();
                build_menu();
            }
            return true;
        }
        int hit = -1;
        for (std::size_t c = card_cache.size(); c-- > 0;) {
            const CardCache& entry = card_cache[c];
            if (entry.index < 0) continue;  // the highlight slot
            if (sx >= entry.base_x && sx < entry.base_x + entry.w &&
                sy >= entry.base_y && sy < entry.base_y + entry.h) {
                hit = entry.index;
                break;
            }
        }
        if (hit < 0) {
            // The sneak-peek tokens: SneakPeakArea::HandleEvent (0x100084D3C) plays
            // Con_SneakPeek_Click (proto 0x7A) and starts the peek, which is Board::SneakPeaker
            // (0x10008B280) turning the whole board over for <SneakPeekIconDur>.
            if (pairs_board.tokens() <= 0) {
                for (const SDL_FRect& r : pairs_tokens) {
                    if (sx < r.x || sx >= r.x + r.w || sy < r.y || sy >= r.y + r.h) continue;
                    // SneakPeakArea::InitUI hides token i when `i + 1 > Round+0x11C`, so a
                    // difficulty with <NumStartTokens>0 shows none and the click lands nowhere.
                    std::printf("  sneak token: none left at this difficulty (NumStartTokens 0)\n");
                    return true;
                }
                return true;
            }
            for (std::size_t t = 0; t < pairs_tokens.size(); ++t) {
                if (t >= static_cast<std::size_t>(pairs_board.tokens())) break;
                const SDL_FRect& r = pairs_tokens[t];
                if (sx < r.x || sx >= r.x + r.w || sy < r.y || sy >= r.y + r.h) continue;
                if (!pairs_board.use_token()) {
                    std::printf("  sneak token %zu: no token left\n", t);
                    return true;
                }
                play_pair_sfx("Con_SneakPeek_Click");
                play_pair_sfx("Con_SneakPeek_Starburst");
                std::printf("  sneak token %zu -> %d left, peek for %d ticks\n", t,
                            pairs_board.tokens(), 30 * pairs_constants.sneak_peak_icon_duration_seconds);
                sync_flips();
                timed_refresh();
                refresh_hud();
                return true;
            }
            return true;
        }
        const pple::ClickResult clicked = pairs_board.click(hit);
        if (!clicked.accepted) return true;
        play_click_sfx(clicked);
        // Board::DoFlyIntoJar, one coin per card that turned over.  A completed board does not
        // end the round here: the jar's own state machine runs the flight and the burst and then
        // calls Jar::StopExplosion (finish_board_end).
        spawn_arcs(clicked.flew);
        if (clicked.relaid_out) {
            // The Mixer reshuffled the cards: Board::DoMixer re-places them, so the art has to be
            // rebuilt.  Deferred to the main loop so the board is never re-dealt from a click.
            pairs_rebuild_pending = true;
            pairs_keep_state = true;
            return true;
        }
        sync_flips();
        timed_refresh();
        refresh_hud();
        std::printf("  click %-3d -> %s, score %d, matches %d/%d%s\n", hit,
                    clicked.match ? "MATCH"
                                  : (clicked.wild_match
                                         ? "WILD (+10)"
                                         : (clicked.mismatch ? "MISS" : "revealed")),
                    pairs_board.score(), pairs_board.matches(), pairs_board.round_pairs_total(),
                    pairs_board.lockout_ticks() > 0 ? ", reveal timer armed" : "");
        return true;
    };
    bool running = true;
    // --script-click "x,y;x,y" injects clicks in scene coordinates through the same handler the
    // mouse uses, so menu -> game -> menu and the flip animation can be verified without a mouse.
    std::vector<std::pair<float, float>> script_clicks;
    if (!script_click_spec.empty()) {
        std::size_t at = 0;
        while (at < script_click_spec.size()) {
            const std::size_t semi = script_click_spec.find(';', at);
            const std::string item = script_click_spec.substr(at, semi - at);
            const std::size_t comma = item.find(',');
            if (comma != std::string::npos) {
                script_clicks.emplace_back(static_cast<float>(std::atof(item.substr(0, comma).c_str())),
                                           static_cast<float>(std::atof(item.substr(comma + 1).c_str())));
            }
            at = semi == std::string::npos ? script_click_spec.size() : semi + 1;
        }
        std::printf("script: %zu click(s)\n", script_clicks.size());
    }
    std::size_t script_next = 0;
    Uint64 script_last = SDL_GetTicks();
    // --bench-build N: time N whole build_pairs calls with every sheet already cached.  That is
    // the cost the reveal timer used to pay on every one of its 30 Hz ticks.
    if (bench_build > 0 && pairs_ready) {
        for (int i = 0; i < bench_build; ++i) {
            ++seed_for_pairs;
            build_pairs();
        }
        std::printf("bench-build: %zu call(s), %.3f ms total, %.3f ms/call\n", build_calls,
                    build_ms, build_calls == 0 ? 0.0 : build_ms / static_cast<double>(build_calls));
        for (SDL_Texture* t : hud_textures) SDL_DestroyTexture(t);
        for (const auto& [name, t] : sheet_cache) SDL_DestroyTexture(t);
        if (font != nullptr) TTF_CloseFont(font);
        TTF_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
    // --bench: render this many frames as fast as possible and report the card-layer cost.
    std::size_t bench_done = 0;
    const Uint64 bench_start = SDL_GetPerformanceCounter();
    Uint64 last_tick = SDL_GetTicks();
    float pairs_accumulator = 0.0f;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else
            if (event.type == SDL_EVENT_MOUSE_MOTION && screen == Screen::Menu && menu_ready &&
                view_scale > 0.0f) {
                running = click_scene((event.motion.x - view_ox) / view_scale,
                                      (event.motion.y - view_oy) / view_scale, true);
            } else
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT && view_scale > 0.0f) {
                running = click_scene((event.button.x - view_ox) / view_scale,
                                      (event.button.y - view_oy) / view_scale, false);
            } else
            if (event.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = event.key.key;
                if (key == SDLK_ESCAPE || key == SDLK_Q) {
                    // In the game ESC goes back to the menu, as ReturnToMainMenu does; from the
                    // menu it quits.
                    if (screen == Screen::Concentration) {
                        pairs_ready = false;
                        screen = Screen::Menu;
                        menu_hover_name.clear();
                        build_menu();
                    } else {
                        running = false;
                    }
                } else
                if ((key == SDLK_R || key == SDLK_N) && pairs_ready) {
                    ++seed_for_pairs;
                    build_pairs();
                } else
                if (!movie_xml.empty() && (key == SDLK_RIGHT || key == SDLK_LEFT || key == SDLK_PAGEDOWN || key == SDLK_PAGEUP)) {
                    const int delta = (key == SDLK_RIGHT || key == SDLK_PAGEDOWN) ? 1 : -1;
                    frame = static_cast<std::size_t>(std::max(0, static_cast<int>(frame) + delta));
                    load_current(true);
                } else
                if (!sprite_xml.empty() && (key == SDLK_RIGHT || key == SDLK_LEFT)) {
                    const std::size_t delta = key == SDLK_RIGHT ? 1 : static_cast<std::size_t>(-1);
                    frame = (frame + delta) % 256;
                    load_current(true);
                } else
                if (screen == Screen::Other && sprite_xml.empty() &&                           (key == SDLK_RIGHT || key == SDLK_DOWN || key == SDLK_PAGEDOWN)) {
                    index = (index + 1) % items.size();
                    load_current(true);
                } else
                if (screen == Screen::Other && sprite_xml.empty() &&                           (key == SDLK_LEFT || key == SDLK_UP || key == SDLK_PAGEUP)) {
                    index = (index + items.size() - 1) % items.size();
                    load_current(true);
                } else
                if (key == SDLK_SPACE) {
                    integer_scale = !integer_scale;
                } else
                if (key == SDLK_S && current.ok) {
                    std::string name = sprite_xml.empty() ? items[index].slot.entry->name : sprite_xml;
                    std::replace(name.begin(), name.end(), '\\', '_');
                    const std::string out = name + ".ppm";
                    std::printf("saving %s: %s\n", out.c_str(), write_ppm(out, current) ? "ok" : "failed");
                }
            }
        }
        int out_w = 0, out_h = 0;
        SDL_GetRenderOutputSize(renderer, &out_w, &out_h);
        // A click that advanced the level or let the Mixer reshuffle re-deals the art here, once
        // the click handler has returned.
        if (pairs_rebuild_pending && pairs_ready) {
            pairs_rebuild_pending = false;
            build_pairs();
        }
        // Purble Pairs runs on the engine's fixed 30 Hz step.
        if (pairs_ready) {
            const Uint64 now = SDL_GetTicks();
            const float elapsed = static_cast<float>(now - last_tick) / 1000.0f;
            last_tick = now;
            pairs_accumulator += elapsed;
            bool ticked = false;
            bool flipped = false;
            while (pairs_accumulator >= (1.0f / 30.0f)) {
                pairs_accumulator -= (1.0f / 30.0f);
                const int before = pairs_board.pending_reveal();
                const int lock_before = pairs_board.lockout_ticks();
                pairs_board.tick();
                if (pairs_board.pending_reveal() != before ||
                    pairs_board.lockout_ticks() != lock_before) {
                    ticked = true;
                    sync_flips();
                }
                // The flip runs on the same 30 Hz step as the engine's Card::Update.
                if (advance_flips()) flipped = true;
                professor.time += 1.0f / 30.0f;
                // The jar (Jar::Update 0x10008373C).  The coins fly for /ArcAnimation/Frames
                // ticks; when the array empties and the board is complete the burst starts; when
                // the burst's movie runs out, Jar::StopExplosion ends the board.
                if (pairs_jar_phase == JarPhase::Flying) {
                    bool landed = false;
                    for (std::size_t i = 0; i < pairs_arcs.size();) {
                        if (++pairs_arcs[i].tick >= pairs_arc_frames) {
                            pairs_arcs.erase(pairs_arcs.begin() + static_cast<std::ptrdiff_t>(i));
                            landed = true;
                        } else {
                            ++i;
                        }
                    }
                    if (landed) jar_grow();  // ArcBehavior::Update's tail calls Jar::DoFiller
                    if (pairs_arcs.empty()) {
                        pairs_jar_phase = JarPhase::Idle;
                        jar_begin_burst();
                        if (pairs_jar_phase == JarPhase::Burst) flipped = true;
                    }
                }
                if (pairs_jar_phase == JarPhase::Burst) {
                    // The engine's default is 30 fps when the library carries no FPS attribute,
                    // and one tick is 1/30 s, so the movie advances one frame per tick.
                    const BurstMovie* burst = load_burst(pairs_burst_name);
                    const int frames = burst != nullptr ? burst->movie.frame_count : 0;
                    pairs_burst_frame += 1.0f;
                    flipped = true;
                    if (frames <= 0 || pairs_burst_frame >= static_cast<float>(frames))
                        finish_board_end();
                }
                // A board that completed with nothing left to fly -- the coin sprite did not
                // resolve, or a scripted hook ended it -- still runs the burst.
                if (pairs_board.end_pending() && pairs_jar_phase == JarPhase::Idle) {
                    jar_begin_burst();
                    if (pairs_jar_phase == JarPhase::Idle) finish_board_end();
                }
                // End of round.  Board::Update's loss goes straight to OnEnterState(3), and
                // OnEnterWonGameState is reached from Jar::StopExplosion, so `round_over` is the
                // signal and `won()` alone is not.
                if (pairs_board.round_over() && !pairs_end_played) {
                    pairs_end_played = true;
                    if (pairs_board.round_won()) {
                        play_pair_sfx("Con_Win");
                        std::printf("  round over: WON at level %d/%d, score %d\n",
                                    pairs_board.level_position() + 1, pairs_board.level_count(),
                                    pairs_board.score());
                    } else {
                        play_pair_sfx("Con_GameOver");
                        std::printf("  round over: LOST, score %d\n", pairs_board.score());
                    }
                }
            }
            if (ticked || flipped) {
                timed_refresh();
                refresh_hud();
            }
        } else
        {
            last_tick = SDL_GetTicks();
        }
        if (bench_frames > 0) {
            ++bench_done;
            if (bench_done >= static_cast<std::size_t>(bench_frames)) {
                const Uint64 t1 = SDL_GetPerformanceCounter();
                const double total_ms = 1000.0 * static_cast<double>(t1 - bench_start) /
                                        static_cast<double>(SDL_GetPerformanceFrequency());
                std::printf("bench: %zu frames in %.1f ms (%.3f ms/frame)\n", bench_done, total_ms,
                            total_ms / static_cast<double>(bench_done));
                std::printf("bench: card layer %zu call(s), %.4f ms total, %.4f ms/call\n",
                            refresh_calls, refresh_total_ms,
                            refresh_calls == 0 ? 0.0
                                               : refresh_total_ms / static_cast<double>(refresh_calls));
                std::printf("bench: full build_pairs %zu call(s), %.3f ms total, %.3f ms/call\n",
                            build_calls, build_ms,
                            build_calls == 0 ? 0.0 : build_ms / static_cast<double>(build_calls));
                running = false;
            }
            if (!offscreen) SDL_Delay(0);
        }
        SDL_SetRenderDrawColor(renderer, 24, 24, 32, 255);
        SDL_RenderClear(renderer);
        // Reserve the top strip for the HUD.
        const int hud_strip = hud_textures.empty() ? 0 : hud_h + 12;
        if (!scene_draws.empty()) {
            const float area_w = static_cast<float>(out_w);
            const float area_h = static_cast<float>(out_h - hud_strip);
            const float sx = scene_w > 0.0f ? area_w / scene_w : 1.0f;
            const float sy = scene_h > 0.0f ? area_h / scene_h : 1.0f;
            float s = std::min(sx, sy);
            // The scene is 800x496.  Flooring the ratio pinned a 1400x900 window to scale 1, so
            // the board was drawn at its native size inside a window more than three times its
            // area.  Fill the window by default and keep SPACE for pixel-exact integer scaling.
            if (integer_scale && s > 1.0f) s = std::floor(s);
            if (s <= 0.0f) s = 1.0f;
            const float ox = (area_w - scene_w * s) * 0.5f;
            const float oy = static_cast<float>(hud_strip) + (area_h - scene_h * s) * 0.5f;
            view_scale = s;
            view_ox = ox;
            view_oy = oy;
            // One scripted click per frame, spaced so a flip or a rebuild can settle.
            if (script_next < script_clicks.size()) {
                const Uint64 now = SDL_GetTicks();
                if (script_next == 0 || now - script_last >= 150) {
                    script_last = now;
                    const std::pair<float, float> c = script_clicks[script_next++];
                    std::printf("script click %zu: scene %.0f,%.0f\n", script_next - 1,
                                static_cast<double>(c.first), static_cast<double>(c.second));
                    running = click_scene(c.first, c.second, false);
                }
            }
            for (const DrawItem& d : scene_draws) {
                if (d.texture == nullptr) continue;
                SDL_SetTextureAlphaMod(d.texture, static_cast<Uint8>(std::clamp(d.alpha, 0, 255)));
                const SDL_FRect dst{
                    ox + d.dst.x * s, oy + d.dst.y * s, d.dst.w * s, d.dst.h * s
                };
                const SDL_FRect* src = (d.src.w > 0.0f && d.src.h > 0.0f) ? &d.src : nullptr;
                if (d.rotation != 0.0f) {
                    SDL_RenderTextureRotated(renderer, d.texture, src, &dst, static_cast<double>(d.rotation),                                             nullptr, SDL_FLIP_NONE);
                } else
                {
                    SDL_RenderTexture(renderer, d.texture, src, &dst);
                }
                SDL_SetTextureAlphaMod(d.texture, 255);
            }
        }
        if (scene_draws.empty() && texture != nullptr) {
            const SDL_FRect fitted =                fit_rect_within(out_w, out_h - hud_strip, static_cast<int>(src_rect.w),                                static_cast<int>(src_rect.h), integer_scale);
            const SDL_FRect dst{
                fitted.x, fitted.y + hud_strip, fitted.w, fitted.h
            };
            SDL_RenderTexture(renderer, texture, &src_rect, &dst);
        }
        if (!hud_textures.empty()) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
            const SDL_FRect panel{
                0.0f, 0.0f, static_cast<float>(out_w), static_cast<float>(hud_strip)
            };
            SDL_RenderFillRect(renderer, &panel);
            float y = 6.0f;
            for (SDL_Texture* t : hud_textures) {
                float tw = 0.0f, th = 0.0f;
                SDL_GetTextureSize(t, &tw, &th);
                const SDL_FRect where{
                    6.0f, y, tw, th
                };
                SDL_RenderTexture(renderer, t, nullptr, &where);
                y += th;
            }
        }
        if (offscreen) {
            if (script_next < script_clicks.size()) {
                // Still working through --script-click: keep rendering, capture on the last one.
                SDL_RenderPresent(renderer);
                SDL_Delay(16);
                continue;
            }
            // --shot-delay lets a flip or an animation reach a known point before the capture.
            if (shot_delay_ms > 0 && SDL_GetTicks() - script_last < static_cast<Uint64>(shot_delay_ms)) {
                SDL_RenderPresent(renderer);
                SDL_Delay(8);
                continue;
            }
            const bool ok = capture(renderer, shot_path);
            SDL_RenderPresent(renderer);
            running = false;
            if (!ok) {
                SDL_DestroyTexture(texture);
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 1;
            }
        } else
        {
            SDL_RenderPresent(renderer);
            SDL_Delay(8);
        }
    }
    for (SDL_Texture* t : hud_textures) SDL_DestroyTexture(t);
    if (texture != nullptr) SDL_DestroyTexture(texture);
    for (const auto& [name, t] : sheet_cache) SDL_DestroyTexture(t);
    if (font != nullptr) TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
