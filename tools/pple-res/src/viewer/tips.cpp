#include "app.hpp"
#include <cctype>

namespace pple::viewer {

// The Professor's speech bubbles.  They are not windows: TipManager turns one <Tip> of
// UI\Concentration\TIPS.XML into a 9-slice panel built from a <TipStyle> document and draws it
// inside the game window, so this module reproduces the shipped mechanism rather than inventing
// one.  The style and layout rules come from:
//
//   TipManager::parseXml        0x1000930B8   <Id>/<Title>/<Message>/<Duration>/<MaxHeight>/
//                                             <MaxWidth>/<MinHeight>/<MinWidth>/<NoTimeout>/
//                                             <CaptureGlobalClick>/<Type>
//   TipManager::ShowTipIfEnabledAtLocV
//                               0x100092AC8   defaults: MinWidth 220, MinHeight 100,
//                                             MaxWidth g_uiLayoutX (800), MaxHeight 350
//   Tip::Initialize             0x1000A5834   the style's pieces and the font colours
//   Tip::Arrange                0x1000A49A0   the box size and the text placement
//
// The one deliberate difference: this drawer keeps the panel's text inside the scene.  The
// engine's MaxWidth default is the whole 800-pixel layout, and a tip anchored at x=615 could
// therefore leave the screen; the shipped <MinWidth> 175 keeps the real tips narrower than
// that, and this clamps to it as well so a longer translated string cannot push art offscreen.

// A font per face name.  `<TitleFontFace>`/`<BodyFontFace>` name "Segoe UI"; the viewer has one
// --font for the HUD, so this tries the named face out of the system font directory and falls
// back to it, which keeps the build portable (no font is required to exist).
//
// The style's <TitleFontSize> 12 / <BodyFontSize> 8 are Windows point sizes (96 dpi); SDL_ttf's
// point size is a 72-dpi pixel size, so the same numbers draw about a quarter smaller here.  The
// scale keeps the bubble's text the size the shipped screen shows.
TTF_Font* App::face_font(const std::string& face, float size) {
    const float pixels = size * (96.0f / 72.0f);
    const int key_size = static_cast<int>(pixels + 0.5f);
    const std::string key = face + "|" + std::to_string(key_size);
    const auto it = face_fonts.find(key);
    if (it != face_fonts.end()) return it->second;
    TTF_Font* font_for_face = nullptr;
    const char* windir = SDL_getenv("WINDIR");
    if (windir != nullptr && !face.empty()) {
        std::string base = face;
        std::string bare;
        for (char c : base)
            if (c != ' ') bare.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        const std::string path = std::string(windir) + "\\Fonts\\" + bare + ".ttf";
        std::ifstream probe(path, std::ios::binary);
        if (probe.good()) font_for_face = TTF_OpenFont(path.c_str(), pixels);
    }
    if (font_for_face == nullptr) font_for_face = font_for(pixels);
    face_fonts[key] = font_for_face;
    return font_for_face;
}

// A localised MUI string by id, or empty.  The difficulty chooser and the end-of-round panel use
// the ids the shipped dialogs use (213/214/215..217/219 and the rank titles).
std::string App::mui_text(int id) {
    const std::u16string* s = mui_strings.find(id);
    return s == nullptr ? std::string{} : pple::utf8_from_utf16(*s);
}

void App::measure_text(const std::string& face, float size, const std::string& text, float wrap,
                       int& w, int& h) {
    w = 0;
    h = 0;
    TTF_Font* f = face_font(face, size);
    if (f == nullptr || text.empty()) return;
    int tw = 0, th = 0;
    if (wrap > 1.0f) {
        if (!TTF_GetStringSizeWrapped(f, text.c_str(), text.size(), wrap, &tw, &th)) return;
    } else if (!TTF_GetStringSize(f, text.c_str(), text.size(), &tw, &th)) {
        return;
    }
    w = tw;
    h = th;
}

void App::append_text(std::vector<DrawItem>& out, const std::string& face, float size,
                      std::uint32_t colour, const std::string& text, float x, float y,
                      float wrap) {
    if (text.empty()) return;
    TTF_Font* f = face_font(face, size);
    if (f == nullptr) return;
    const SDL_Color colour_value{static_cast<Uint8>((colour >> 16) & 0xFF),
                                 static_cast<Uint8>((colour >> 8) & 0xFF),
                                 static_cast<Uint8>(colour & 0xFF),
                                 static_cast<Uint8>((colour >> 24) & 0xFF)};
    SDL_Surface* surface = wrap > 1.0f
        ? TTF_RenderText_Blended_Wrapped(f, text.c_str(), text.size(), colour_value,
                                         static_cast<int>(wrap + 0.5f))
        : TTF_RenderText_Blended(f, text.c_str(), text.size(), colour_value);
    if (surface == nullptr) return;
    DrawItem d;
    d.texture = SDL_CreateTextureFromSurface(renderer, surface);
    d.dst = SDL_FRect{x, y, static_cast<float>(surface->w), static_cast<float>(surface->h)};
    d.alpha = 255;
    SDL_DestroySurface(surface);
    if (d.texture == nullptr) return;
    out.push_back(d);
}

// UI\Concentration\TIPS.XML, once per process.  The titles and messages are localisation
// references ("|42881|CON_Tip_GamePlay|Hello! ..."), so every one goes through the same
// localiser the scene labels use, which is the .mui when --mui was given.
void App::load_tips() {
    if (tips_loaded) return;
    tips_loaded = true;
    const auto* slot = bundle.find("UI\\CONCENTRATION\\TIPS.XML");
    if (slot == nullptr) {
        std::printf("  tips: UI\\Concentration\\TIPS.XML not found\n");
        return;
    }
    const std::vector<std::uint8_t> data = bundle.read(*slot);
    pple::XmlNode root;
    std::string error;
    if (!pple::xml_parse(data, root, error)) {
        std::printf("  tips: TIPS.XML: %s\n", error.c_str());
        return;
    }
    for (const pple::XmlNode& node : root.children) {
        if (lower(node.name) != "tip") continue;
        // A <Tip>'s values are localisation references; the numeric and boolean ones are plain
        // but go through the same helper so a localised build cannot change their meaning.
        const auto value = [&](const char* name) {
            return pple::localized_default(node.child_text(name));
        };
        TipDef def;
        def.id = node.child_text("Id");
        def.title = value("Title");
        def.message = value("Message");
        def.type = node.child_text("Type");
        def.duration = static_cast<float>(std::atof(value("Duration").c_str()));
        def.no_timeout = lower(value("NoTimeout")) == "true";
        def.capture_click = lower(value("CaptureGlobalClick")) != "false";
        def.min_width = std::atoi(value("MinWidth").c_str());
        def.min_height = std::atoi(value("MinHeight").c_str());
        def.max_width = std::atoi(value("MaxWidth").c_str());
        def.max_height = std::atoi(value("MaxHeight").c_str());
        if (!def.id.empty()) tip_defs.push_back(std::move(def));
    }
    std::printf("  tips: UI\\Concentration\\TIPS.XML, %zu tip(s)\n", tip_defs.size());
}

bool App::load_tip_slice(TipSlice& slice, const std::string& xml_name, int state) {
    const Sprite sprite = resolve_sprite(bundle, xml_name, state);
    if (!sprite.ok) {
        std::printf("  tip style: %s did not resolve\n", xml_name.c_str());
        return false;
    }
    std::string how;
    const auto* slot = bundle.resolve(sprite.requested, how);
    if (slot == nullptr) return false;
    std::string note;
    slice.tex = load_sheet_texture(sprite.requested, slot->entry->name, note);
    if (slice.tex == nullptr) return false;
    slice.src = sprite.src;
    slice.w = sprite.src.w;
    slice.h = sprite.src.h;
    return true;
}

// Tips\Styles\Green\TipStyle.xml names the nine panel pieces and the two fonts; the layout
// offsets default to TitleOffset 5,0, BodyOffset 5,0, CloseButtonOffset 6,6 (Tip::Initialize
// 0x1000A6458..0x1000A64CB).
bool App::load_tip_style() {
    if (tip_style.ok) return true;
    const auto* slot = bundle.find("TIPS\\STYLES\\GREEN\\TIPSTYLE.XML");
    if (slot == nullptr) {
        std::printf("  tip style: TIPS\\STYLES\\GREEN\\TIPSTYLE.XML not found\n");
        return false;
    }
    const std::vector<std::uint8_t> data = bundle.read(*slot);
    pple::XmlNode root;
    std::string error;
    if (!pple::xml_parse(data, root, error)) {
        std::printf("  tip style: %s\n", error.c_str());
        return false;
    }
    const std::string prefix = "TIPS\\STYLES\\GREEN\\";
    bool ok = true;
    ok &= load_tip_slice(tip_style.back, prefix + "BACKSPRITE.XML", 0);
    ok &= load_tip_slice(tip_style.left, prefix + "LEFTBORDERSPRITE.XML", 0);
    ok &= load_tip_slice(tip_style.right, prefix + "RIGHTBORDERSPRITE.XML", 0);
    ok &= load_tip_slice(tip_style.top, prefix + "TOPBORDERSPRITE.XML", 0);
    ok &= load_tip_slice(tip_style.bottom, prefix + "BOTTOMBORDERSPRITE.XML", 0);
    ok &= load_tip_slice(tip_style.ul, prefix + "ULSPRITE.XML", 0);
    ok &= load_tip_slice(tip_style.ur, prefix + "URSPRITE.XML", 0);
    ok &= load_tip_slice(tip_style.ll, prefix + "LLSPRITE.XML", 0);
    ok &= load_tip_slice(tip_style.lr, prefix + "LRSPRITE.XML", 0);
    ok &= load_tip_slice(tip_style.close, prefix + "CLOSEBUTTON.XML", 0);
    load_tip_slice(tip_style.info, prefix + "INFOSPRITE.XML", 0);  // optional
    // Every style value may be a localisation reference ("|49820|Tips|8//..."), which is how
    // NodeLabel::Load (0x1000A09F0) treats <FontName>/<FontSize> too, so each one goes through
    // the same localiser before it is used.
    const auto value = [&](const char* name) { return pple::localized_default(root.child_text(name)); };
    tip_style.title_face = value("TitleFontFace");
    tip_style.body_face = value("BodyFontFace");
    if (tip_style.title_face.empty()) tip_style.title_face = "Segoe UI";
    if (tip_style.body_face.empty()) tip_style.body_face = "Segoe UI";
    const std::string title_size = value("TitleFontSize");
    const std::string body_size = value("BodyFontSize");
    if (!title_size.empty()) tip_style.title_size = static_cast<float>(std::atof(title_size.c_str()));
    if (!body_size.empty()) tip_style.body_size = static_cast<float>(std::atof(body_size.c_str()));
    const std::string title_colour = value("TitleFontColor");
    const std::string body_colour = value("BodyFontColor");
    if (!title_colour.empty())
        tip_style.title_colour = static_cast<std::uint32_t>(std::strtoul(title_colour.c_str(), nullptr, 16));
    if (!body_colour.empty())
        tip_style.body_colour = static_cast<std::uint32_t>(std::strtoul(body_colour.c_str(), nullptr, 16));
    tip_style.ok = ok;
    if (!tip_style.ok) std::printf("  tip style: one or more pieces did not load\n");
    return tip_style.ok;
}

const TipDef* App::find_tip(const std::string& id) const {
    for (const TipDef& def : tip_defs)
        if (def.id == id) return &def;
    return nullptr;
}

// The style's own 9-slice: back, the four stretched edges, then the four corners.  The same
// pieces frame a tip bubble, a choice panel and the end-of-round summary.
void App::append_panel(std::vector<DrawItem>& out, float px, float py, float width, float height) {
    const auto piece = [&](const TipSlice& slice, float dx, float dy, float dw, float dh) {
        if (slice.tex == nullptr || dw <= 0.0f || dh <= 0.0f) return;
        DrawItem d;
        d.texture = slice.tex;
        d.src = slice.src;
        d.dst = SDL_FRect{px + dx, py + dy, dw, dh};
        d.alpha = 255;
        out.push_back(d);
    };
    piece(tip_style.back, tip_style.ul.w - 1.0f, tip_style.ul.h - 1.0f,
          width - tip_style.ul.w - tip_style.ur.w + 2.0f,
          height - tip_style.ul.h - tip_style.ll.h + 2.0f);
    piece(tip_style.left, 0.0f, tip_style.ul.h, tip_style.left.w,
          height - tip_style.ul.h - tip_style.ll.h);
    piece(tip_style.right, width - tip_style.right.w, tip_style.ur.h, tip_style.right.w,
          height - tip_style.ur.h - tip_style.lr.h);
    piece(tip_style.top, tip_style.ul.w, 0.0f, width - tip_style.ul.w - tip_style.ur.w,
          tip_style.top.h);
    piece(tip_style.bottom, tip_style.ll.w, height - tip_style.bottom.h,
          width - tip_style.ll.w - tip_style.lr.w, tip_style.bottom.h);
    piece(tip_style.ul, 0.0f, 0.0f, tip_style.ul.w, tip_style.ul.h);
    piece(tip_style.ur, width - tip_style.ur.w, 0.0f, tip_style.ur.w, tip_style.ur.h);
    piece(tip_style.ll, 0.0f, height - tip_style.ll.h, tip_style.ll.w, tip_style.ll.h);
    piece(tip_style.lr, width - tip_style.lr.w, height - tip_style.lr.h, tip_style.lr.w,
          tip_style.lr.h);
}

void App::hide_tip() {
    free_direct(tip.owned);
    tip.draws.clear();
    tip.owned.clear();
    tip.visible = false;
    tip.id.clear();
    tip.wait_click = false;
    tip.seconds_left = 0.0f;
    rebuild_overlay();
}

// Tip::Arrange (0x1000A49A0), minus its info-sprite branch, which no Concentration tip uses:
//
//   width  = title width + left + right + 2 * TitleOffsetX, clamped to [MinWidth, MaxWidth]
//   textw  = width - left - right - 2 * BodyOffsetX
//   height = body height + TitleOffsetY + top + bottom + 2 * BodyOffsetY
//
// and the pieces are placed on those edges.  A position outside the scene is pulled back inside
// it, so a long localised tip cannot draw off-screen.
void App::show_tip(const std::string& id, float x, float y) {
    if (!tips_enabled) return;
    load_tips();
    if (!load_tip_style()) return;
    const TipDef* def = find_tip(id);
    if (def == nullptr) {
        std::printf("  tip %s: not in TIPS.XML\n", id.c_str());
        return;
    }
    if (g_mui_for_labels == nullptr && def->title.empty() && def->message.empty()) return;
    hide_tip();
    tip.id = id;
    const float title_offset_x = 5.0f, title_offset_y = 0.0f;
    const float body_offset_x = 5.0f, body_offset_y = 0.0f;

    // Render the two labels first and size the box from what they actually are: SDL_ttf's own
    // wrapped-size query under-reports a long paragraph's height, and the panel has to fit the
    // text this drawer is about to put in it.
    std::vector<DrawItem> scratch;
    append_text(scratch, tip_style.title_face, tip_style.title_size, tip_style.title_colour,
                def->title, 0.0f, 0.0f, 0.0f);
    const float title_w = scratch.empty() ? 0.0f : scratch.front().dst.w;
    const float title_h = scratch.empty() ? 0.0f : scratch.front().dst.h;

    const float min_width = def->min_width > 0 ? static_cast<float>(def->min_width) : 220.0f;
    const float max_width = def->max_width > 0 ? static_cast<float>(def->max_width)
                                               : static_cast<float>(pple::kScreenWidth);
    float width = title_w + tip_style.left.w + tip_style.right.w + title_offset_x * 2.0f;
    width = std::max(width, min_width);
    width = std::min(width, max_width);
    const float text_width = std::max(40.0f, width - tip_style.left.w - tip_style.right.w -
                                                body_offset_x * 2.0f);
    std::vector<DrawItem> body;
    append_text(body, tip_style.body_face, tip_style.body_size, tip_style.body_colour,
                def->message, 0.0f, 0.0f, text_width);
    const float body_w = body.empty() ? 0.0f : body.front().dst.w;
    const float body_h = body.empty() ? 0.0f : body.front().dst.h;
    // Tip::Arrange widens the box when the wrapped body needs more room than the title did.
    if (body_w > text_width)
        width = std::min(body_w + tip_style.left.w + tip_style.right.w + body_offset_x * 2.0f,
                         max_width);
    float height = title_h + title_offset_y + tip_style.top.h + tip_style.bottom.h +
                   body_offset_y * 2.0f + body_h;
    const float max_height = def->max_height > 0 ? static_cast<float>(def->max_height) : 350.0f;
    height = std::min(height, max_height);

    float px = x, py = y;
    px = std::clamp(px, 4.0f, std::max(4.0f, static_cast<float>(pple::kScreenWidth) - width - 4.0f));
    py = std::clamp(py, 4.0f, std::max(4.0f, static_cast<float>(pple::kScreenHeight) - height - 4.0f));
    tip.rect = SDL_FRect{px, py, width, height};
    append_panel(tip.draws, px, py, width, height);

    // The title sits on the top edge inset by TitleOffset, the body under it at BodyOffset.
    for (DrawItem& d : scratch) {
        d.dst.x = px + tip_style.left.w + title_offset_x;
        d.dst.y = py + tip_style.top.h + title_offset_y;
        tip.owned.push_back(d);
    }
    for (DrawItem& d : body) {
        d.dst.x = px + tip_style.left.w + body_offset_x;
        d.dst.y = py + tip_style.top.h + title_offset_y + title_h + body_offset_y;
        tip.owned.push_back(d);
    }
    // The close button, top right at CloseButtonOffset (6,6).
    if (tip_style.close.tex != nullptr) {
        DrawItem d;
        d.texture = tip_style.close.tex;
        d.src = tip_style.close.src;
        d.dst = SDL_FRect{px + width - tip_style.close.w - 6.0f, py + 6.0f, tip_style.close.w,
                          tip_style.close.h};
        d.alpha = 255;
        tip.draws.push_back(d);
        tip.close = d.dst;
    }
    tip.visible = true;
    tip.wait_click = def->no_timeout;
    // <CaptureGlobalClick> defaults to true, which is what makes the "click anywhere to carry on"
    // bubbles go away.  It is applied only to a tip that has no timeout: one that expires by
    // itself must not also swallow the player's next card click.
    tip.captures = def->capture_click && def->no_timeout;
    tip.seconds_left = def->duration;
    rebuild_overlay();
    std::printf("  tip %s: \"%s\" at %.0f,%.0f %.0fx%.0f (body %.0fx%.0f of %zu chars)%s\n",
                id.c_str(), def->title.c_str(), static_cast<double>(px), static_cast<double>(py),
                static_cast<double>(width), static_cast<double>(height),
                static_cast<double>(body_w), static_cast<double>(body_h), def->message.size(),
                def->no_timeout ? ", waits for a click" : "");
}

// A draw item's label texture is created here and is not in sheet_cache, so it has to be freed
// explicitly; the sprite pieces are borrowed and must survive.
void App::free_direct(std::vector<DrawItem>& out) {
    for (const DrawItem& d : out) {
        if (d.texture == nullptr) continue;
        bool cached = false;
        for (const auto& entry : sheet_cache)
            if (entry.second == d.texture) cached = true;
        if (!cached) SDL_DestroyTexture(d.texture);
    }
    out.clear();
}

void App::release_overlay() {
    free_direct(tip.owned);
    free_direct(result_draws);
    overlay_draws.clear();
}

// Everything drawn over the live board: the end-of-round panel first, the tip bubble on top.
// Both vectors borrow sheet textures, so only the items themselves are copied here.
void App::rebuild_overlay() {
    overlay_draws.clear();
    for (const DrawItem& d : result_draws) overlay_draws.push_back(d);
    for (const DrawItem& d : tip.draws) overlay_draws.push_back(d);
    for (const DrawItem& d : tip.owned) overlay_draws.push_back(d);
}

} // namespace pple::viewer
