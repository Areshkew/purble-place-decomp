#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "pple/cab.hpp"
#include "pple/concentration.hpp"
#include "pple/movie.hpp"
#include "pple/mui.hpp"
#include "pple/rgmh.hpp"
#include "pple/wma.hpp"
#include "pple/pe_image.hpp"
#include "pple/png.hpp"
#include "pple/scene.hpp"
#include "pple/xml.hpp"
#include <functional>
#include <optional>
#include <utility>

namespace pple::viewer {
    using pple::PeImage;
    using pple::ResourceEntry;
    enum class Kind {
        Png, Cab, Jpeg, Xml, Audio, Other
    };
    std::string normalise(const std::string& s);
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

std::string lower(std::string s);
const char* kind_name(Kind k);
Kind classify(std::span<const std::uint8_t> d);
bool surface_to_rgba(SDL_Surface* surface, Decoded& out);
Decoded decode_payload(std::span<const std::uint8_t> payload, Kind kind);
std::vector<std::uint8_t> slurp_file(const std::string& path);
bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes);
bool write_ppm(const std::string& path, const Decoded& image);
bool capture(SDL_Renderer* renderer, const std::string& path);
SDL_FRect fit_rect_within(int area_w, int area_h, int img_w, int img_h, bool integer_scale);
std::string sht_sibling(const std::string& sht, const char* ext);
Sprite resolve_sprite(const Bundle& bundle, const std::string& xml_name, int state);

extern pple::MuiStrings* g_mui_for_labels;
extern std::size_t g_mui_hits, g_mui_misses;
std::string mui_resolve(int id, const std::string& fallback, void*);

} // namespace pple::viewer
