# Viewer module map

Start here, then search the relevant module. Do not read the whole viewer to change
one feature. All files here use the internal `pple::viewer` namespace; command-line
flags and the core library's public interfaces are unchanged.

## Where to look

| Responsibility | File and entry points |
|---|---|
| Process entry | [view_main.cpp](../view_main.cpp): constructs `App`, calls `run` |
| Arguments, DLL/MUI loading, SDL setup, initial screen | [app.cpp](app.cpp): `App::run` |
| Events, 30 Hz updates, frame drawing, scripts, captures, benchmarks, teardown | [loop.cpp](loop.cpp): `run_loop` |
| Mouse/script hit testing and screen navigation | [input.cpp](input.cpp): `click_scene` |
| Menu, loading screen, drifting clouds | [menu.cpp](menu.cpp): `build_menu`, `build_loading`, `load_menu_clouds` |
| Resource resolution, image decoding, sprite XML, file/PPM I/O, localiser | [resources.hpp](resources.hpp), [resources.cpp](resources.cpp): `Bundle`, `resolve_sprite`, `decode_payload`, `capture` |
| Sheet texture cache, colour/alpha composition, coverage | [textures.cpp](textures.cpp): `load_sheet_texture`, `read_resource` |
| Scene XML draw list, labels, draw-list release | [scene.cpp](scene.cpp): `build_scene`, `append_label`, `release_draws` |
| Skeletal movie evaluation and part placement | [movie.cpp](movie.cpp): `build_movie`, `append_movie` |
| Individual image/sprite browser | [browser.cpp](browser.cpp): `load_current` |
| Audio manifest, PCM playback, cached Pairs sounds | [audio.hpp](audio.hpp), [audio.cpp](audio.cpp): `initialise_pair_audio`, `play_click_sfx` |
| Fonts and diagnostic HUD | [hud.cpp](hud.cpp): `font_for`, `rebuild_hud`, `refresh_hud` |
| Pairs setup orchestration, deal/reset, solve/click hooks, scene visibility | [pairs_setup.cpp](pairs_setup.cpp): `build_pairs` |
| Per-level card back, jar points/coins, arc and burst configuration, animation reset | [level_art.cpp](level_art.cpp): `build_pair_level_art` |
| Card face/back/outline/blank/spark cache and time-bar assets | [card_assets.cpp](card_assets.cpp): `build_pair_card_assets` |
| Progress indicators, count-aware sneak tokens, sneak movie loading | [pair_hud.cpp](pair_hud.cpp): `build_pair_hud` |
| Cached card/effect draw lists, jar layering, timer/token/Professor draws | [pairs_render.cpp](pairs_render.cpp): `refresh_cards`, `timed_refresh` |
| Flips, match sparks, flying jar coins, burst start/cache | [pairs_effects.cpp](pairs_effects.cpp): `advance_flips`, `advance_card_fx`, `spawn_arcs` |
| Board completion, score/rank, settings save | [pairs_round.cpp](pairs_round.cpp): `finish_board_end`, `report_round_end` |
| Professor state machine, tips, transitions | [professor.cpp](professor.cpp): `professor_show`, `professor_display_tip`, `professor_tip_closed`, `professor_advance`, `professor_round_end`, `professor_intro_begin`, `professor_level_tip` |
| Tip panel: TIPS.XML, the green 9-slice, text and fonts | [tips.cpp](tips.cpp): `load_tips`, `load_tip_style`, `show_tip`, `append_panel`, `append_text`, `face_font`, `rebuild_overlay` |
| In-canvas difficulty chooser and end-of-round panel | [overlays.cpp](overlays.cpp): `build_difficulty`, `draw_difficulty`, `append_button`, `build_result_panel`, `draw_result_panel`, `start_game`, `game_to_menu` |
| State and small rendering records | [app.hpp](app.hpp), [types.hpp](types.hpp) |
| Game rules (no SDL) | [concentration.cpp](../concentration.cpp), [concentration.hpp](../../include/pple/concentration.hpp) |

## Ownership and ordering

`App` is a single, non-copyable session owner. Its private fields replace the former
`main` locals; module methods share that state rather than copying boards or caches.
This is a structural split, not a new game engine or a redesign of SDL lifetimes.
SDL setup, early exits and teardown keep their previous order. The core loader's
resource-fetch adapter captures the stable `App` address.

Sheet textures belong to the session cache. Draw items only borrow them; scene
rebuilds release directly-created label textures but preserve cached sheets.
The immutable `pairs_scene_layer` is retained between card refreshes. Do not add
resource/XML loads or full `build_pairs` calls to the per-tick card refresh.

`build_pairs` calls level-art, card-assets and Pairs-HUD setup in the original order.
The frame loop still dispatches the same 30 Hz updates and appends jar layers in
the same order. Audio cache/localiser data retain their original process lifetime.

Three layers draw over the frame, in this order, and each has one owner:

* `scene_draws` — the layout and the card layer, rebuilt by `refresh_cards`/`build_pairs`;
* `picker_draws` — the difficulty chooser's own screen, owned by [overlays.cpp](overlays.cpp)
  and drawn instead of the scene while `screen == Screen::Difficulty`;
* `overlay_draws` — the tip bubble and the end-of-round panel over the live board,
  reassembled by `rebuild_overlay` from `tip.draws`, `tip.owned` and `result_draws`.

A panel and a button are the same 9-slice, built by `append_panel` from the loaded
`Tips\Styles\Green` pieces with `Tip::Arrange`'s geometry (the back inset by the left and top
borders). `append_button` centres its name and description between the top and bottom borders;
`draw_result_panel` redraws the end-of-round panel without reprinting its summary line, which is
what lets its buttons highlight on mouse motion.

A draw item's texture is either borrowed from `sheet_cache` or created for a label. Only the
second kind may be destroyed on a rebuild, which is what `free_direct` checks; the tip's label
textures live in `tip.owned` and the panels' in `result_draws`, so `release_overlay` and
`free_direct` are the only places that free them.

The Professor's six movies are parsed once into `prof_movie_cache` and the live one is copied
into `professor.movie`; `professor_advance` runs from the 30 Hz tick in [loop.cpp](loop.cpp) and
is the only place the state machine moves. Do not add movie or sheet loads to it.

## Verification and future changes

Build with the commands in the [project README](../../README.md). The offline full
matrix and assertions are recorded in notes section 20.6. Session 9 checks live in
`C:\re\traces\cpp`: `s9-run-regression.ps1`, `s9-compare-regression.py`,
`s9-extra-probes.ps1` and `s9-check-moved-bodies.py`. They do not ship game assets.
The body-token check uses the saved pre-refactor source; it is evidence for this
refactor, not a permanent restriction on later feature work.

The original 29-command matrix and 62 output assertions pass before/after, with
one correction: benchmark card-refresh counts depend on wall-clock timing, so
assert the presence of the metric rather than a fixed 34 calls. Compare scores,
state transitions and one-full-build invariants; do not compare benchmark times
or generated-save timestamps as deterministic values. Real-time animation shots
may differ by a frame; still movie/scene captures and token-row pixels are stronger
pixel comparisons.

Keep this map current. Add behavior to the owning module, not the entry point or
a catch-all helper. Split a module further only at a cohesive boundary; keep state
ownership and the relevant regression commands explicit. Functional changes and
structural moves should be separate milestones.
