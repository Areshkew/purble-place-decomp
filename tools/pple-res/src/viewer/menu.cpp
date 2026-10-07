#include "app.hpp"

namespace pple::viewer {

void App::load_menu_clouds() {
    if (menu_clouds_loaded) return;
    menu_clouds_loaded = true;
    std::vector<std::uint8_t> bytes;
    const auto fetch = [&](const std::string& name, std::vector<std::uint8_t>& out) {
        std::string inner_how;
        const auto* s = bundle.resolve(name, inner_how);
        if (s == nullptr) return false;
        out = bundle.read(*s);
        return true;
    };
    std::string error;
    if (!fetch(menu_clouds_xml, bytes) ||
        !pple::movie_load(bytes, fetch, menu_clouds, error) || menu_clouds.parts.empty()) {
        std::printf("menu clouds: %s did not load (%s)\n", menu_clouds_xml.c_str(),
                    error.c_str());
        return;
    }
    menu_cloud_fps = menu_clouds.parts.empty()
                         ? menu_clouds.fps
                         : pple::movie_frame_rate(menu_clouds, menu_clouds.parts.front());
    menu_clouds_ok = true;
    std::printf("menu clouds: %s, %d frame(s) at %.0f fps, %zu part(s), at %.0f,%.0f\n",
                menu_clouds_xml.c_str(), menu_clouds.frame_count,
                static_cast<double>(menu_cloud_fps), menu_clouds.parts.size(),
                static_cast<double>(menu_clouds_x), static_cast<double>(menu_clouds_y));
}

// Engine_SetBackMode(1, L"UI\\PurblePlace\\Loading.xml") is called by both
// PurblePlaceManager::Init (0x100070A62) and ConcentrationGame::LoadGameUI (0x100081008), and
// Engine_SetBackMode (0x10009D424) hands the name to RenderManager::SetBack.  Loading.xml is
// one 800x496 sprite on UI\PurblePlace\Loading.jpg with <Anchor>center</Anchor>.
// PurblePlaceManager::Init loads the two <NodeLabel Name="Loading"> labels and hides them at
// the end of Init (0x100070F25), so they are on screen for exactly as long as the load lasts.
void App::build_loading() {
    release_draws();
    scene_w = scene_h = 0.0f;
    menu_hotspots.clear();
    const Sprite back = resolve_sprite(bundle, "UI\\PurblePlace\\Loading.xml", 0);
    std::string how;
    const auto* slot = back.ok ? bundle.resolve(back.requested, how) : nullptr;
    if (back.ok && slot != nullptr) {
        std::string note;
        SDL_Texture* tex = load_sheet_texture(back.requested, slot->entry->name, note);
        if (tex != nullptr) {
            pple::SceneItem geo;
            geo.src_x = back.src.x;
            geo.src_y = back.src.y;
            geo.src_w = back.src.w;
            geo.src_h = back.src.h;
            geo.frames = back.frames;
            geo.per_row = back.per_row;
            geo.prepped = back.prepped;
            float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
            pple::sprite_frame_rect(geo, 0, fx, fy, fw, fh);
            DrawItem d;
            d.texture = tex;
            d.src = SDL_FRect{fx, fy, fw, fh};
            d.dst = SDL_FRect{0.0f, 0.0f, fw, fh};
            d.alpha = 255;
            scene_draws.push_back(d);
            scene_w = std::max(scene_w, fw);
            scene_h = std::max(scene_h, fh);
        }
    }
    std::size_t labels = 0;
    std::string mhow;
    const auto* mslot = bundle.resolve(menu_xml, mhow);
    if (mslot != nullptr) {
        const auto fetch = [&](const std::string& name, std::vector<std::uint8_t>& out) {
            std::string inner_how;
            const auto* s = bundle.resolve(name, inner_how);
            if (s == nullptr) return false;
            out = bundle.read(*s);
            return true;
        };
        pple::Scene scene;
        std::string error;
        if (pple::scene_load(bundle.read(*mslot), fetch, scene, error)) {
            for (const pple::SceneItem& item : scene.items) {
                const std::string& key = item.name.empty() ? item.instance : item.name;
                if (item.kind != pple::NodeKind::Label || key != "Loading") continue;
                if (append_label(item)) ++labels;
            }
        }
    }
    scene_w = std::max(scene_w, pple::kScreenWidth);
    scene_h = std::max(scene_h, pple::kScreenHeight);
    std::printf("loading: UI\\PurblePlace\\Loading.xml + %zu Loading label(s)\n", labels);
    hud_lines = {"UI\\PurblePlace\\Loading.xml", "the loading screen, back mode 1"};
    rebuild_hud();
}

void App::build_menu() {
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
            if (!append_label(item)) continue;
            ++drawn;
            continue;
        }
        if (item.kind != pple::NodeKind::Sprite && item.kind != pple::NodeKind::Button) {
            continue;
        }
        if (item.sheet.empty()) continue;
        // The engine starts a button at animation 0 and moves to 1 on <enter> and 2 on <down>
        // (NodeButton::HandleEvent 0x1000AAC92 / 0x1000AAE1C); the focus animation runs to its
        // own last frame, and every button here has a one-frame hover art, so the frame choice
        // only matters if the art ever gains frames.  See pairs_setup.cpp for the same rule.
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
        pple::sprite_frame_rect(geometry, hovered ? sprite.frames - 1 : 0, fx, fy, fw, fh);
        // NodeSprite::UpdateZero (0x100093740) subtracts the scaled registration offset.
        const float dx = (item.x - sprite.reg_x * item.scale) * item.scale;
        const float dy = (item.y - sprite.reg_y * item.scale) * item.scale;
        DrawItem d;
        d.texture = tex;
        d.src = SDL_FRect{fx, fy, fw, fh};
        d.dst = SDL_FRect{dx, dy, fw * item.scale, fh * item.scale};
        d.alpha = item.alpha;
        scene_draws.push_back(d);
        // The cloud container is a child of the Background, so the clouds go right here.
        // scene_load names a <Background>path</Background> shorthand "<parent>/Background".
        const bool is_background =
            key == "Background" ||
            (key.size() > 11 && key.compare(key.size() - 11, 11, "/Background") == 0);
        if (is_background) {
            load_menu_clouds();
            menu_cloud_at = scene_draws.size();
            // A hover rebuild must not restart the drift.
            if (menu_cloud_start == 0) menu_cloud_start = SDL_GetTicks();
        }
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
}

} // namespace pple::viewer
