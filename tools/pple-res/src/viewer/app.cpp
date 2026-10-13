#include "app.hpp"
#include "audio.hpp"

namespace pple::viewer {

    void usage() {
        std::fprintf(stderr,                 "pple-view — SDL3 viewer for the Purble Place resource DLLs\n\n"                 "  pple-view <pe>... [options]\n\n"                 "  --resource NAME   start on this resource\n"                 "  --sprite XML      show one frame of a <Node Type=\"Sprite\"> definition\n"                 "  --scene XML       render a UI layout: every instance with its sheet frame\n"                 "  --movie XML       render a <Movie> skeletal animation at one frame\n"                 "  --movie-name NAME in --scene mode, draw this <Movie> animation slot at --anchor\n"                 "  --pairs [D,L,S]   play Purble Pairs: D = Beginner|Intermediate|Advanced, L = level, S = seed\n"                 "  --menu            start on the real main menu (UI\\PurblePlace\\PurblePlaceManager.xml); its\n"                 "                    Purble Pairs hot-spot enters the game\n"                 "  --mce             draw the Media Center build's New Game / Exit Game header buttons.\n"                 "                    The shipped executable deletes those five nodes when\n"                 "                    g_bMediaCenter == 0 (0x10008122B), so by default they are absent, as\n"                 "                    they are in this build\n"                 "  --no-container-offsets  do not apply the engine's HUD re-parenting (notes 17.7)\n"
                 "  --hidpi           draw the menu, the loading screen and Purble Pairs from the game's own\n"
                 "                    double-DPI art: the x2 sheets and x2 images the engine loads when\n"
                 "                    Windows reports a high DPI (g_bDoubleDPI).  The layout is unchanged and\n"
                 "                    every source rectangle is read at twice its offset and size.\n"
                 "  --no-hidpi        force the 1x art; this is also the automatic choice below a 1.5x\n"
                 "                    window scale, where the x2 art would have to be minified\n"
                 "  --difficulty      start on the in-canvas Select Difficulty screen\n"
        "  --cakes [D]       start a Comfy Cakes round: D = Beginner|Intermediate|Advanced\n"
        "  --cakes-solve     with --cakes, play the whole round out through the click path\n"
        "  --shop [D]        start a Purble Shop round: D = Beginner|Intermediate|Advanced\n"
        "  --shop-seed N     the round's template, variants and hidden face (default 1)\n"
        "  --shop-dump       print the room's hot-spots after every build\n"
        "  --shop-guess S    judge a selection per feature: \"c0,c1,c2,c3,c4;...\"\n"
                 "  --intro           with --pairs, also run the Professor's entrance and intro tips\n"
                 "  --no-intro        never run them (an explicit --pairs start skips them anyway)\n"
                 "  --no-tips         do not draw the Professor's TIPS.XML speech bubbles\n"
                 "  --anchor X,Y      origin for --movie, or for --movie-name in --scene mode\n"
                 "  --play NAME       decode and play an AUDIO.XML event key or a resource name\n"
                 "  --volume N        playback volume, 0..100 (default 100)\n"
                 "  --mute            silence Pairs and Comfy Cakes sound effects\n"
                  "  --debug           show diagnostic HUD (hidden during normal play)\n"
                  "  --no-debug        hide it even for captures and benchmarks\n"
                  "  --console         open/attach a Windows console for diagnostics\n"
                 "  --audio-dir PATH  play <Event>.wav from this pack instead of decoding the WMA\n"
                 "                    resources; the browser build needs it, because Media Foundation\n"
                 "                    is the only decoder this project has.  See web/README.md\n"
                 "  --pairs-state PATH  save the round when the player returns to the menu, and\n"
                 "                      resume it (with its board thumbnail) when the file holds one\n"
                 "  --pairs-click I,J  flip these cell indices, through the mouse handler\n"                 "  --frame N         frame index within the sprite (default 0)\n"                 "  --state N         which <Animation> of a Button node to show (default 0;\n"                 "                     the engine starts at 0 and moves to 1 on <enter>, 2 on <down>)\n"                 "  --list            list viewable resources and exit\n"                 "  --shot FILE.ppm   render one frame offscreen, write it, exit\n"                 "  --width N --height N   window size (default 1400x900)\n"                 "  --font PATH       TTF for the HUD (default C:\\Windows\\Fonts\\arial.ttf)\n"                 "  --all             include resources this viewer cannot decode\n\n"                 "  keys: LEFT/RIGHT or PGUP/PGDN navigate, SPACE toggles integer scaling,\n"                 "        in --menu/--pairs: click a hot-spot, N new game, R reshuffle, ESC quits\n"                 "        (ESC in the Concentration screen returns to the menu)\n");
    }
    std::size_t parse_index(const char* text) {
        return static_cast<std::size_t>(std::strtoul(text, nullptr, 10));
    }

int App::run(int argc, char** argv) {

    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        usage();
        return args.empty() ? 1 : 0;
    }

    int debug_mode = 0;  // auto: inspection/capture hooks retain their diagnostic HUD
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
        else if (a == "--loading") {
            loading_mode = true;
            // An optional millisecond count; Engine_SetBackMode's screen is up for however long
            // PurblePlaceManager::Init takes, which the reconstruction cannot measure.
            if (i + 1 < args.size() && args[i + 1][0] != '-') loading_ms = std::atoi(args[++i].c_str());
        }
        else if (a == "--mce") mce_mode = true;
        else if (a == "--difficulty") difficulty_mode = true;
        else if (a == "--cakes-solve") cakes_solve_hook = true;
        else if (a == "--cakes") {
            cakes_mode = true;
            // An optional difficulty, the way --pairs takes one.
            if (i + 1 < args.size() && args[i + 1][0] != '-') {
                const std::string d = args[++i];
                cakes_difficulty_choice = d == "Intermediate" ? 1 : (d == "Advanced" ? 2 : 0);
            }
        }
        else if (a == "--shop") {
            shop_mode = true;
            // An optional difficulty, the way --cakes takes one.
            if (i + 1 < args.size() && args[i + 1][0] != '-') {
                const std::string d = args[++i];
                shop_difficulty_choice = d == "Intermediate" ? 1 : (d == "Advanced" ? 2 : 0);
            }
        }
        else if (a == "--shop-seed" && i + 1 < args.size())
            shop_choice_seed = static_cast<unsigned>(std::strtoul(args[++i].c_str(), nullptr, 10));
        else if (a == "--shop-dump") shop_dump = true;
        else if (a == "--shop-guess" && i + 1 < args.size()) shop_guess_spec = args[++i];
        else if (a == "--intro") intro_wanted = true;        else if (a == "--no-intro") intro_wanted = false;
        else if (a == "--deal") deal_enabled = true;
        else if (a == "--no-deal") deal_enabled = false;
        else if (a == "--no-tips") tips_enabled = false;
        else if (a == "--no-container-offsets") container_offsets = false;
        else if (a == "--hidpi") hidpi_mode = 1;
        else if (a == "--no-hidpi") hidpi_mode = -1;
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
        else if (a == "--debug") debug_mode = 1;
        else if (a == "--no-debug") debug_mode = -1;
        else if (a == "--console") {}  // Windows bootstrap owns optional console setup
        else if (a == "--audio-dir" && i + 1 < args.size()) audio_dir = args[++i];
        else if (a == "--pairs-click" && i + 1 < args.size()) pairs_click = args[++i];
        else if (a == "--pairs-save" && i + 1 < args.size()) pairs_save_path = args[++i];
        else if (a == "--pairs-state" && i + 1 < args.size()) pairs_state_path = args[++i];
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

    const bool game_mode = menu_mode || loading_mode || difficulty_mode || cakes_mode ||
                           shop_mode || !pairs_spec.empty();
    debug_hud = debug_mode > 0 || (debug_mode == 0 &&
        (!game_mode || !shot_path.empty() || bench_frames > 0 || bench_build > 0));

    // Before any screen is built: the sound pack location is session state, not per-screen.
    set_sfx_audio_dir(audio_dir);

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

    for (const std::string& m : modules) {
        std::string error;
        if (!bundle.load(m, error)) {
            std::fprintf(stderr, "%s: %s\n", m.c_str(), error.c_str());
            return 1;
        }
    }

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
        if (!load_audio_pcm(bundle, *slot, play_name, audio, error)) {
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
#ifdef __EMSCRIPTEN__
    // SDL takes keyboard events from the element this hint names; the web shell focuses the
    // canvas when the session starts, so the game owns the keyboard without a click on the page.
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    offscreen = !shot_path.empty();
    SDL_WindowFlags window_flags = offscreen ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
#ifdef __EMSCRIPTEN__
    // The page fits a fixed canvas with CSS. Letting SDL resize its backing store
    // from the transformed CSS bounds creates feedback and unnecessary DPI rebuilds.
    window_flags = offscreen ? SDL_WINDOW_HIDDEN : 0;
#endif
    window = SDL_CreateWindow(debug_hud ? "Purble Place — diagnostics" : "Purble Place",
                              width, height, window_flags);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#ifndef __EMSCRIPTEN__
    SDL_SetRenderVSync(renderer, bench_frames > 0 ? 0 : 1);
#endif
    std::printf("renderer: %s\n", SDL_GetRendererName(renderer));

    // The starting DPI mode.  The loop re-decides on every frame -- a resize switches it -- so
    // this only saves a rebuild on the first frame.  It has to guess the HUD strip the loop
    // reserves before the first screen has measured it, and that strip is 63 px on the menu and
    // 80 px on a Pairs board here, so the guess subtracts a deliberately generous 100: guessing
    // 1x where the loop wants 2x costs one cheap rebuild, the other way round loads every x2
    // sheet twice.
    if (hidpi_mode > 0) {
        sheet_dpi = SheetDpi::Double;
    } else if (hidpi_mode < 0) {
        sheet_dpi = SheetDpi::Base;
    } else {
        int out_w = width, out_h = height;
        SDL_GetRenderOutputSize(renderer, &out_w, &out_h);
        const float start_scale =
            std::min(static_cast<float>(out_w) / pple::kScreenWidth,
                     static_cast<float>(std::max(0, out_h - (debug_hud ? 100 : 0))) / pple::kScreenHeight);
        sheet_dpi = start_scale >= hidpi_scale ? SheetDpi::Double : SheetDpi::Base;
    }

    if (TTF_Init()) {
        font = TTF_OpenFont(font_path.c_str(), 15.0f);
        if (font == nullptr)            std::fprintf(stderr, "TTF_OpenFont(%s): %s\n", font_path.c_str(), SDL_GetError());
    } else
    {
        std::fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
    }

    // Adapter for the core loaders; all ordinary viewer callbacks are named methods.
    fetch_resource = [this](const std::string& name, std::vector<std::uint8_t>& out) {
        return read_resource(name, out);
    };

    // --menu needs the Concentration document as well, because its Purble Pairs hot-spot enters
    // the game: the menu is the entry point, not a separate mode.
    // A saved round from a previous run.  ConcentrationGame::LoadGameState (the shipped game's
    // own resume path) puts a "saved game" entry on the menu; here the file is read once and the
    // menu offers to resume it.
    if (!pairs_state_path.empty() && pairs_spec.empty()) read_pair_state(pairs_state_path);
    if (menu_mode && scene_xml.empty())
        scene_xml = "UI\\CONCENTRATION\\CONCENTRATIONGAME.XML";
    if (!pairs_spec.empty()) {
        screen = Screen::Concentration;
        load_current(true);
        // An explicit --pairs start is the analysis hook: it goes straight to the board.  The
        // menu path and --intro ask for the Professor's entrance and his intro bubbles, and
        // Board::Update calls DisplayIntro only once the deal-in has finished, so build_pairs
        // starts them from there rather than here.
    } else if (difficulty_mode) {
        // The in-canvas "Select Difficulty" screen on its own, for a capture or a manual run.
        build_difficulty();
    } else if (cakes_mode) {
        start_cakes(cakes_difficulty_choice);
    } else if (shop_mode) {
        // Purble Shop's document is the game screen itself, exactly as the Comfy Cakes one is:
        // BuildAPurbleGame::LoadGameUI followed by NewGame, not a layout to be measured.
        shop_seed = shop_choice_seed;
        start_shop(shop_difficulty_choice);
        // --shop-guess drives the round through the same shop_guess a click reaches.
        if (!shop_guess_spec.empty()) {
            std::size_t at = 0;
            while (at < shop_guess_spec.size()) {
                const std::size_t semi = shop_guess_spec.find(';', at);
                const std::string entry = shop_guess_spec.substr(at, semi - at);
                int colors[5] = {-1, -1, -1, -1, -1};
                std::size_t next = 0;
                int feature = 0;
                while (next < entry.size() && feature < 5) {
                    const std::size_t comma = entry.find(',', next);
                    colors[feature++] = std::atoi(entry.substr(next, comma - next).c_str());
                    if (comma == std::string::npos) break;
                    next = comma + 1;
                }
                for (int slot = 0; slot < 5; ++slot) shop_model.set_selected(slot, colors[slot]);
                shop_guess();
                at = semi == std::string::npos ? shop_guess_spec.size() : semi + 1;
            }
        }
    } else if (scene_xml == "UI\\COMFYCAKES\\COMFYCAKESGAME.XML") {
        // The Comfy Cakes document is the game screen itself, not a layout to be measured: it is
        // reached as a round, exactly as PurblePlaceManager::Show reaches it from the menu house
        // (ComfyCakesGame::LoadGameUI, then NewGame).  Rendering it as a plain scene would leave a
        // room with no cake on it and buttons that answer nothing.
        start_cakes(cakes_difficulty_choice);
    } else if (menu_mode) {
        if (loading_mode) {
            screen = Screen::Loading;
            loading_start = SDL_GetTicks();
            build_loading();
        } else {
            screen = Screen::Menu;
            build_menu();
        }
    } else if (loading_mode) {
        screen = Screen::Loading;
        loading_start = SDL_GetTicks();
        build_loading();
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

    return run_loop();
}

} // namespace pple::viewer
