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

// Tips\Styles\<name>\TipStyle.xml names the nine panel pieces and the fonts; the layout
// offsets default to TitleOffset 5,0, BodyOffset 5,0, CloseButtonOffset 6,6 (Tip::Initialize
// 0x1000A6458..0x1000A64CB).
//
// The nine piece paths are read out of the document rather than rebuilt from a fixed prefix:
// the engine keeps three separate style pointers, one per <Type>, and the viewer has to do the
// same -- a <Type>Error</Type> tip is the Red panel, not a green one (see load_tip_styles).
bool App::load_tip_style(TipStyle& style, const std::string& name) {
    if (style.ok) return true;
    const std::string doc = "TIPS\\STYLES\\" + name + "\\TIPSTYLE.XML";
    const auto* slot = bundle.find(doc);
    if (slot == nullptr) {
        std::printf("  tip style %s: %s not found\n", name.c_str(), doc.c_str());
        return false;
    }
    const std::vector<std::uint8_t> data = bundle.read(*slot);
    pple::XmlNode root;
    std::string error;
    if (!pple::xml_parse(data, root, error)) {
        std::printf("  tip style %s: %s\n", name.c_str(), error.c_str());
        return false;
    }
    const auto piece = [&](TipSlice& slice, const char* element) {
        return load_tip_slice(slice, pple::localized_default(root.child_text(element)), 0);
    };
    bool ok = true;
    ok &= piece(style.back, "Back");
    ok &= piece(style.left, "LeftBorder");
    ok &= piece(style.right, "RightBorder");
    ok &= piece(style.top, "TopBorder");
    ok &= piece(style.bottom, "BottomBorder");
    ok &= piece(style.ul, "ULCorner");
    ok &= piece(style.ur, "URCorner");
    ok &= piece(style.ll, "LLCorner");
    ok &= piece(style.lr, "LRCorner");
    ok &= piece(style.close, "CloseButton");
    piece(style.info, "InfoSprite");  // optional
    // Every style value may be a localisation reference ("|49820|Tips|8//..."), which is how
    // NodeLabel::Load (0x1000A09F0) treats <FontName>/<FontSize> too, so each one goes through
    // the same localiser before it is used.
    const auto value = [&](const char* element) {
        return pple::localized_default(root.child_text(element));
    };
    style.title_face = value("TitleFontFace");
    style.body_face = value("BodyFontFace");
    if (style.title_face.empty()) style.title_face = "Segoe UI";
    if (style.body_face.empty()) style.body_face = "Segoe UI";
    const std::string title_size = value("TitleFontSize");
    const std::string body_size = value("BodyFontSize");
    if (!title_size.empty()) style.title_size = static_cast<float>(std::atof(title_size.c_str()));
    if (!body_size.empty()) style.body_size = static_cast<float>(std::atof(body_size.c_str()));
    const std::string title_colour = value("TitleFontColor");
    const std::string body_colour = value("BodyFontColor");
    if (!title_colour.empty())
        style.title_colour = static_cast<std::uint32_t>(std::strtoul(title_colour.c_str(), nullptr, 16));
    if (!body_colour.empty())
        style.body_colour = static_cast<std::uint32_t>(std::strtoul(body_colour.c_str(), nullptr, 16));
    style.ok = ok;
    if (!style.ok)
        std::printf("  tip style %s: one or more pieces did not load\n", name.c_str());
    return style.ok;
}

// TipManager keeps one parsed style document per <Type>, at `this + 3 * type + 8` with the type
// index TipManager::parseXml (0x1000930B8) assigns: Intro 0, anything else Help 1, Error 2.  The
// three document paths are fixed in the image and each names its own pieces:
//
//   0x1000FDBF0  TIP_STYLE_INTRO_XML_FILE   Tips\Styles\White\TipStyle.xml
//   0x1000FDC08  TIP_STYLE_ERROR_XML_FILE   Tips\Styles\Red\TipStyle.xml
//   0x1000FDC20  TIP_STYLE_HELP_XML_FILE     Tips\Styles\Green\TipStyle.xml
//
// so a <Type>Intro</Type> bubble is the White frame, <Type>Error</Type> the Red one, and
// everything else -- 25 of the shipped 28 tips -- the Green one.  Only the White and Red styles
// frame a Concentration tip: TIPS.XML's only <Type>Intro</Type> entries are the Professor's and
// its only <Type>Error</Type> entry is BlankCard, while every flip, sneak, jar and level-change
// tip is <Type>Intro</Type> by omission (parseXml defaults <Type> to 1, "Help").
void App::load_tip_styles() {
    if (tip_styles_loaded) return;
    tip_styles_loaded = true;
    load_tip_style(tip_styles["White"], "White");
    load_tip_style(tip_styles["Red"], "Red");
    load_tip_style(tip_styles["Green"], "Green");
    std::printf("  tip styles: White %s, Red %s, Green %s\n",
                tip_styles["White"].ok ? "ok" : "MISSING",
                tip_styles["Red"].ok ? "ok" : "MISSING",
                tip_styles["Green"].ok ? "ok" : "MISSING");
}

// The <Type> of one <Tip> to the style document it selects.  parseXml compares <Type> against
// "Intro" then "Error" and leaves 1 for everything else, and ShowTipIfEnabledAtLocV indexes the
// style table with that number (0x100092C06), so the mapping is by *name*, not by the numeric
// index the parse produces.
const TipStyle& App::tip_style_for(const TipDef& def) {
    load_tip_styles();
    if (def.type == "Error") return tip_styles["Red"];
    if (def.type == "Intro") return tip_styles["White"];
    return tip_styles["Green"];
}

// The green set, which frames the two in-canvas dialogs as well as every "Help"-type tip.
const TipStyle& App::green_style() {
    load_tip_styles();
    return tip_styles["Green"];
}

const TipDef* App::find_tip(const std::string& id) const {
    for (const TipDef& def : tip_defs)
        if (def.id == id) return &def;
    return nullptr;
}

// The style's own 9-slice: back, the four stretched edges, then the four corners.  The same
// pieces frame a tip bubble, a choice panel and the end-of-round summary.
void App::append_panel(std::vector<DrawItem>& out, const TipStyle& style, float px, float py,
                       float width, float height) {
    const auto piece = [&](const TipSlice& slice, float dx, float dy, float dw, float dh) {
        if (slice.tex == nullptr || dw <= 0.0f || dh <= 0.0f) return;
        DrawItem d;
        d.texture = slice.tex;
        d.src = slice.src;
        d.dst = SDL_FRect{px + dx, py + dy, dw, dh};
        d.alpha = 255;
        out.push_back(d);
    };
    // Tip::Arrange (0x1000A49A0) places the back at exactly (LeftBorder.w, TopBorder.h) and
    // sizes it (width - left - right) x (height - top - bottom), i.e. flush against the inside
    // of the four borders, and stretches each border between its two corner sprites.  The
    // corners are drawn last, over the ends of the borders.  An inset here -- the back used to
    // start at ULCorner.w - 1, which is 12 px against a 3 px left border -- leaves a strip of
    // whatever is behind the panel showing, which is the seam the chooser and the tip bubbles
    // used to have.
    piece(style.back, style.left.w, style.top.h,
          width - style.left.w - style.right.w,
          height - style.top.h - style.bottom.h);
    piece(style.left, 0.0f, style.ul.h, style.left.w,
          height - style.ul.h - style.ll.h);
    piece(style.right, width - style.right.w, style.ur.h, style.right.w,
          height - style.ur.h - style.lr.h);
    piece(style.top, style.ul.w, 0.0f, width - style.ul.w - style.ur.w,
          style.top.h);
    piece(style.bottom, style.ll.w, height - style.bottom.h,
          width - style.ll.w - style.lr.w, style.bottom.h);
    piece(style.ul, 0.0f, 0.0f, style.ul.w, style.ul.h);
    piece(style.ur, width - style.ur.w, 0.0f, style.ur.w, style.ur.h);
    piece(style.ll, 0.0f, height - style.ll.h, style.ll.w, style.ll.h);
    piece(style.lr, width - style.lr.w, height - style.lr.h, style.lr.w,
          style.lr.h);
}

void App::append_panel(std::vector<DrawItem>& out, float px, float py, float width, float height) {
    load_tip_styles();
    append_panel(out, tip_styles["Green"], px, py, width, height);
}

void App::hide_tip() {
    free_direct(tip.owned);
    tip.draws.clear();
    tip.owned.clear();
    tip.visible = false;
    tip.id.clear();
    tip.wait_click = false;
    tip.simple = false;
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
    const TipDef* def = find_tip(id);
    if (def == nullptr) {
        load_tips();
        def = find_tip(id);
    }
    show_tip_def(def, id, x, y);
}

// The body of show_tip, split out so show_simple_tip can use a definition it already has.
void App::show_tip_def(const TipDef* def, const std::string& id, float x, float y) {
    if (!tips_enabled) return;
    if (def == nullptr) return;
    load_tip_styles();
    // TipManager picks the panel from the <Type> (ShowTipIfEnabledAtLocV's `this + 3 * type + 8`
    // at 0x100092C06), so an <Type>Error</Type> tip is framed in red, an <Type>Intro</Type> one in
    // white and every other one in the green set.
    const TipStyle& style = tip_style_for(*def);
    if (!style.ok) return;
    if (g_mui_for_labels == nullptr && def->title.empty() && def->message.empty()) return;
    hide_tip();
    tip.id = id;
    const float title_offset_x = 5.0f, title_offset_y = 0.0f;
    const float body_offset_x = 5.0f, body_offset_y = 0.0f;

    // Render the two labels first and size the box from what they actually are: SDL_ttf's own
    // wrapped-size query under-reports a long paragraph's height, and the panel has to fit the
    // text this drawer is about to put in it.
    std::vector<DrawItem> scratch;
    append_text(scratch, style.title_face, style.title_size, style.title_colour,
                def->title, 0.0f, 0.0f, 0.0f);
    const float title_w = scratch.empty() ? 0.0f : scratch.front().dst.w;
    const float title_h = scratch.empty() ? 0.0f : scratch.front().dst.h;

    const float min_width = def->min_width > 0 ? static_cast<float>(def->min_width) : 220.0f;
    const float max_width = def->max_width > 0 ? static_cast<float>(def->max_width)
                                               : static_cast<float>(pple::kScreenWidth);
    float width = title_w + style.left.w + style.right.w + title_offset_x * 2.0f;
    width = std::max(width, min_width);
    width = std::min(width, max_width);
    const float text_width = std::max(40.0f, width - style.left.w - style.right.w -
                                                body_offset_x * 2.0f);
    std::vector<DrawItem> body;
    append_text(body, style.body_face, style.body_size, style.body_colour,
                def->message, 0.0f, 0.0f, text_width);
    const float body_w = body.empty() ? 0.0f : body.front().dst.w;
    const float body_h = body.empty() ? 0.0f : body.front().dst.h;
    // Tip::Arrange widens the box when the wrapped body needs more room than the title did.
    if (body_w > text_width)
        width = std::min(body_w + style.left.w + style.right.w + body_offset_x * 2.0f,
                         max_width);
    float height = title_h + title_offset_y + style.top.h + style.bottom.h +
                   body_offset_y * 2.0f + body_h;
    const float max_height = def->max_height > 0 ? static_cast<float>(def->max_height) : 350.0f;
    height = std::min(height, max_height);

    float px = x, py = y;
    px = std::clamp(px, 4.0f, std::max(4.0f, static_cast<float>(pple::kScreenWidth) - width - 4.0f));
    py = std::clamp(py, 4.0f, std::max(4.0f, static_cast<float>(pple::kScreenHeight) - height - 4.0f));
    tip.rect = SDL_FRect{px, py, width, height};
    append_panel(tip.draws, style, px, py, width, height);

    // The title sits on the top edge inset by TitleOffset, the body under it at BodyOffset.
    for (DrawItem& d : scratch) {
        d.dst.x = px + style.left.w + title_offset_x;
        d.dst.y = py + style.top.h + title_offset_y;
        tip.owned.push_back(d);
    }
    for (DrawItem& d : body) {
        d.dst.x = px + style.left.w + body_offset_x;
        d.dst.y = py + style.top.h + title_offset_y + title_h + body_offset_y;
        tip.owned.push_back(d);
    }
    // The close button, top right at CloseButtonOffset (6,6).
    if (style.close.tex != nullptr) {
        DrawItem d;
        d.texture = style.close.tex;
        d.src = style.close.src;
        d.dst = SDL_FRect{px + width - style.close.w - 6.0f, py + 6.0f, style.close.w,
                          style.close.h};
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
    std::printf("  tip %s: \"%s\" at %.0f,%.0f %.0fx%.0f (%s body %.0fx%.0f of %zu chars)%s\n",
                id.c_str(), def->title.c_str(), static_cast<double>(px), static_cast<double>(py),
                static_cast<double>(width), static_cast<double>(height), def->type.c_str(),
                static_cast<double>(body_w), static_cast<double>(body_h), def->message.size(),
                def->no_timeout ? ", waits for a click" : "");
}

// The engine's second family of tips: mui-less bubbles shown straight from a node's own
// FocusGot/click art -- `CONTipManager::ShowSimpleTip` (0x100085C8C) calls
// `TipManager::ShowTipIfEnabledAtLoc(tip, x, y)` with no arguments.  It reuses the same panel,
// but it is not part of the Professor's state machine, so closing one must not move him.
void App::show_simple_tip(const std::string& id, float x, float y) {
    if (!tips_enabled) return;
    load_tips();
    const TipDef* def = find_tip(id);
    if (def == nullptr) {
        std::printf("  tip %s: not in TIPS.XML\n", id.c_str());
        return;
    }
    show_tip_def(def, id, x, y);
    tip.simple = true;
}

// A hover bubble: shown while the pointer stays on its object, hidden when it leaves.  This is
// called every tick as well as on motion, so it converges instead of rebuilding: the wanted id
// is compared with the one that is up, and a bubble whose <Duration> has already expired with the
// pointer still on the object is not raised again until that object loses the pointer.
void App::update_simple_tip(const std::string& id, float x, float y) {
    if (tip.simple && !tip.visible) hover_simple_shown.clear();  // it timed out or was closed
    // The steady state: nothing is wanted and nothing is up.
    if (id.empty() && hover_simple_shown.empty() && !(tip.visible && tip.simple)) return;
    if (id == hover_simple_shown) return;
    if (tip.visible && tip.wait_click) return;  // a "click to carry on" bubble owns the screen
    if (!id.empty() && id == hover_simple_expired) return;
    hover_simple_shown = id;
    if (id.empty()) {
        if (tip.visible && tip.simple) hide_tip();
        return;
    }
    // Moving onto another object clears the expiry latch for the one just left.
    if (!id.empty()) hover_simple_expired.clear();
    show_simple_tip(id, x, y);
}

// The hover bubbles, keyed off the same hit test click_scene uses.  The engine's own handlers for
// these two objects are:
//
//   SneakPeakArea::HandleEvent  0x100084D3C  MouseEnter over a token (event type 2, whose only
//                                            non-PeakArea child node is a SneakPeakToken button)
//                                            shows the "SneakPeak" tip at 615,125; the PeakArea
//                                            background itself returns before the tip
//   Card::HandleEvent           0x10007C1F4  a MouseDown (case 1) on a matched cell, whose node
//                                            is "BlankCard" or "FaceAnimation" in state 4, shows
//                                            the "BlankCard" tip at 615,125
//
// The reconstruction draws both keys on focus, i.e. on hover, because that is where the player
// looks for them here; see the viewer README's Hover section.  It runs on every 30 Hz tick as
// well as on motion, because a match can turn the card under a still pointer into a BlankCard.
void App::update_hover_tip() {
    if (!pairs_ready || result_shown || screen != Screen::Concentration) {
        update_simple_tip(std::string{}, 0.0f, 0.0f);
        return;
    }
    if (tip.visible && tip.wait_click) return;
    if (hover_token >= 0 && hover_token < static_cast<int>(pairs_board.tokens())) {
        update_simple_tip("SneakPeak", 615.0f, 125.0f);
        return;
    }
    if (hover_card >= 0 && hover_card < static_cast<int>(card_fx.size()) &&
        card_fx[static_cast<std::size_t>(hover_card)].blank) {
        update_simple_tip("BlankCard", 615.0f, 125.0f);
        return;
    }
    update_simple_tip(std::string{}, 0.0f, 0.0f);
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
