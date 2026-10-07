#include "app.hpp"

namespace pple::viewer {

void App::build_pair_hud(std::string& scene_how) {
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
    pairs_token_draws.clear();
    pairs_token_hover_draws.clear();
    pairs_sneak_frame = -1.0f;
    if (loaded_scene) {
        float mx = 0.0f, my = 0.0f;
        bool have_meter = false, have_sneak = false;
        for (const pple::SceneItem& item : loaded_scene->containers) {
            const std::string& key = item.name.empty() ? item.instance : item.name;
            if (key == "ProgressMeterContainer") {
                mx = item.x;
                my = item.y;
                have_meter = true;
            } else if (key == "SneakPeekContainer") {
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
            // scene_load reads each token's own x/y/scale; apply_container_offsets adds
            // SneakPeekContainer. LoadSneakPeekToken (0x10008C48C) does the same.
            for (const pple::SceneItem& item : loaded_scene->items) {
                const std::string& key = item.name.empty() ? item.instance : item.name;
                if (key != "SneakPeekToken" || item.sheet.empty()) continue;
                std::string how;
                const auto* slot = bundle.resolve(item.sheet, how);
                if (slot == nullptr) continue;
                DrawItem d;
                d.texture = load_sheet_texture(item.sheet, slot->entry->name, pairs_sheet_note);
                if (d.texture == nullptr) continue;
                d.src = SDL_FRect{item.src_x + (item.prepped ? 1.0f : 0.0f),
                                  item.src_y + (item.prepped ? 1.0f : 0.0f),
                                  item.src_w, item.src_h};
                d.dst = SDL_FRect{item.x - item.reg_x * item.scale,
                                  item.y - item.reg_y * item.scale,
                                  item.src_w * item.scale, item.src_h * item.scale};
                d.alpha = item.alpha;
                pairs_tokens.push_back(d.dst);
                pairs_token_draws.push_back(d);
                // A token is a NodeButton (ConUILoader::LoadSneakPeekToken 0x10008C48C names it
                // "Button" and sets the two clickable/focusable flags at node+74/+75), so its
                // second <Animation> is the hover art -- SneakPeakButton.xml keeps the idle eye
                // on concentrationSheet29 and the hover and pressed eyes on sheet12.
                DrawItem hover = d;
                const Sprite hs = resolve_sprite(bundle, item.ref, 1);
                if (hs.ok) {
                    std::string hover_how;
                    const auto* hover_slot = bundle.resolve(hs.requested, hover_how);
                    if (hover_slot != nullptr) {
                        SDL_Texture* hover_tex =
                            load_sheet_texture(hs.requested, hover_slot->entry->name, pairs_sheet_note);
                        if (hover_tex != nullptr) {
                            hover.texture = hover_tex;
                            hover.src = SDL_FRect{hs.src.x + (hs.prepped ? 1.0f : 0.0f),
                                                  hs.src.y + (hs.prepped ? 1.0f : 0.0f),
                                                  hs.src.w, hs.src.h};
                        }
                    }
                }
                pairs_token_hover_draws.push_back(hover);
                std::printf("  sneak token %zu: %.0f,%.0f %.0fx%.0f, scale %.0f%%\n",
                            pairs_tokens.size() - 1, static_cast<double>(d.dst.x),
                            static_cast<double>(d.dst.y), static_cast<double>(d.dst.w),
                            static_cast<double>(d.dst.h), static_cast<double>(item.scale * 100.0f));
            }
            pairs_sneak_x = -18.0f;
            pairs_sneak_y = -70.0f;
            // SneakPeakArea::InitUI (0x100084B20) creates the movie Base under its
            // input a2 (game Base), NOT SneakPeekContainer; SetPosition at 0x100084B35.
            std::vector<std::uint8_t> bytes;
            if (!pairs_sneak_movie_ok && fetch_resource(scene_xml, bytes)) {
                pple::XmlNode root;
                std::string error;
                if (pple::xml_parse(bytes, root, error)) {
                    if (const auto* node = root.find("SneakMovie")) {
                        if (const auto* desc = node->find("Desc")) {
                            const std::string name = desc->child_text("Movie");
                            if (fetch_resource(name, bytes)) {
                                pairs_sneak_movie_ok = pple::movie_load(bytes, fetch_resource,
                                                                      pairs_sneak_movie, error);
                                std::printf("  sneak movie: %s, %d frame(s), %zu part(s), anchor %.0f,%.0f%s\n",
                                            name.c_str(), pairs_sneak_movie.frame_count,
                                            pairs_sneak_movie.parts.size(),
                                            static_cast<double>(pairs_sneak_x),
                                            static_cast<double>(pairs_sneak_y),
                                            pairs_sneak_movie_ok ? "" : " FAILED");
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace pple::viewer
