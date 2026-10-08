#include "app.hpp"

namespace pple::viewer {

void App::build_pair_level_art(const std::vector<std::uint8_t>& scene_bytes, std::string& scene_how) {
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
    // Per-level state that has to be cleared before anything reads the board: the jar starts
    // each level empty (Jar::InitUI 0x100083593 zeroes Jar+0xC8, and the burst's own wipe has
    // already hidden the previous level's coins), and the card animation belongs to the board
    // that is being replaced.
    pairs_arcs.clear();
    pairs_arc_sparks.clear();
    pairs_jar_fill = 0;
    pairs_jar_phase = JarPhase::Idle;
    pairs_burst_frame = 0.0f;
    card_fx.assign(pairs_board.cell_count(), CardFx{});
    card_fx_pending_fly.clear();
    // Card::InitUI's tail shows the BlankCard for a card already in state 2 and the art
    // otherwise, so a rebuild -- after a level change, the Mixer, or a scripted run that has
    // already played cards out -- keeps the matched cells blank.
    for (std::size_t i = 0; i < card_fx.size(); ++i) {
        const bool matched = pairs_board.face(static_cast<int>(i)) == pple::CardFace::Matched;
        card_fx[i].blank = matched;
        card_fx[i].doomed = matched;
    }
    card_flip.assign(pairs_board.cell_count(), CardFlip{});
    card_face_shown.assign(pairs_board.cell_count(), pple::CardFace::Hidden);
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
            // The tray's fill follows the board, so a scripted hook that has already played
            // cards out (its clicks ran before this block) still shows the pile, and a fresh
            // or re-dealt level starts empty because Round+0x12C is 0 for it.
            jar_grow();
            // Jar::LoadMovies (0x1000835D0): `(Board+0x20)[i] + 1032` for i = 0..3, so the
            // burst movie is per level.  Jar+0x30 selects `Jar+0x48[Round+0x124]`.  The
            // document itself is loaded lazily, when the burst starts.
            pairs_burst_name = (at < backs.size()) ? backs[at].movie : std::string{};
            pairs_burst_anchor_x = pairs_jar_origin_x + movie_container_x;
            pairs_burst_anchor_y = pairs_jar_origin_y + movie_container_y;
        }
    }
}

} // namespace pple::viewer
