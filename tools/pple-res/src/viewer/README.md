# Viewer module map

Start here, then search the relevant module. Do not read the whole viewer to change
one feature. All files here use the internal `pple::viewer` namespace; command-line
flags and the core library's public interfaces are unchanged.

## Focused Pairs reward checks

The eye-card token reward belongs to `ConcentrationBoard::click` in
[concentration.cpp](../concentration.cpp). `SneakPeakArea::AddPeak` caps inventory at three.
[concentration_test.cpp](../../tests/concentration_test.cpp) covers reward/cap/mismatch/wild
matching and earned-peek duration/cost. [concentration_reward_regression.py](../../tests/concentration_reward_regression.py)
checks normal clicks, inventory pixels, saved tokens and token use/expiry in both DPI modes:
`concentration_reward_regression.py BUILD_DIR SAMPLE_DIR FRESH_TRACE_DIR`.

## Where to look

| Responsibility | File and entry points |
|---|---|
| Process entry | [view_main.cpp](../view_main.cpp): constructs `App`, calls `run` (a long-lived `App` in the browser build, because the main loop unwinds `main`) |
| Arguments, DLL/MUI loading, SDL setup, initial screen | [app.cpp](app.cpp): `App::run` |
| Events, 30 Hz updates, frame drawing, scripts, captures, benchmarks, teardown | [loop.cpp](loop.cpp): `run_loop` (setup and dispatch), `frame_step` (one frame; the browser calls it from `emscripten_set_main_loop`), `finish_loop`, `set_sheet_dpi`, `rebuild_current_screen` |
| Mouse/script hit testing, hover (focus) and screen navigation | [input.cpp](input.cpp): `click_scene`, `update_hover` |
| Menu, loading screen, drifting clouds | [menu.cpp](menu.cpp): `build_menu`, `build_loading`, `load_menu_clouds` |
| Comfy Cakes: the room, its draw order, the engine's own clones | [cakes_screen.cpp](cakes_screen.cpp): `build_cakes`, `cakes_append`, `start_cakes`, `cakes_dump_draws` |
| Comfy Cakes: the round's state over the SDL-free model | [cakes_play.cpp](cakes_play.cpp): `cakes_new_game`, `cakes_use_tool`, `cakes_slide_to`, `cakes_slide_left`, `cakes_slide_right`, `cakes_judge`, `cakes_compute_score`, `cakes_draw_cake`, `cakes_refresh`, `cakes_redraw`, `cakes_solve` |
| Comfy Cakes: difficulty-specific machine visibility/slots, idle parts and tool animations | [cakes_tools.cpp](cakes_tools.cpp): `cakes_tool_visible`, `cakes_tool_slot_for`, `cakes_tools_load`, `cakes_tools_start`, `cakes_tools_shake`, `cakes_tools_advance`, `cakes_tools_draw` |
| Purble Shop: the room, its documents and the cabinet | [shop_screen.cpp](shop_screen.cpp): `start_shop`, `build_shop`, `shop_refresh`, `shop_append`, `shop_draw_cabinet`, `shop_draw_face`; records in [shop_screen.hpp](shop_screen.hpp) |
| Purble Shop: curtain/feedback timeline | [shop_flow.hpp](shop_flow.hpp): App-owned `ShopFlow`; `shop_advance` in [shop_screen.cpp](shop_screen.cpp), called by [loop.cpp](loop.cpp); tests in [shop_test.cpp](../../tests/shop_test.cpp) and [shop_regression.py](../../tests/shop_regression.py) |
| Purble Shop: the round over the SDL-free model | [shop_screen.cpp](shop_screen.cpp): `shop_new_game`, `shop_select`, `shop_guess`, `shop_draw_history`, `shop_draw_counts`; rules in [shop.cpp](../shop.cpp) |
| Comfy Cakes: normal-round timer, indicator, inlet spawn schedule and line-clog check | [cakes_clock.cpp](cakes_clock.cpp): `cakes_clock_advance`, `cakes_add_paper`; SDL-free state in [cakes_clock.hpp](cakes_clock.hpp), owned by `App`, reset only on a new round; tests in [cakes_clock_test.cpp](../../tests/cakes_clock_test.cpp) and [cakes_clock_regression.py](../../tests/cakes_clock_regression.py) |
| Comfy Cakes: belt/roller clocks, independent inlet motion, wrapping, dispatch, trash and shipping readouts | [cakes_transport.cpp](cakes_transport.cpp): `cakes_transport_advance`, `cakes_transport_draw`, `cakes_transport_finish_slide`, `cakes_next_target`; state owned by `App::cakes_transport` in [cakes_transport.hpp](cakes_transport.hpp) |
| ResourceAnimation parsing/evaluation, cue times and integer drop integration (SDL-free) | [animation.cpp](../animation.cpp), [animation.hpp](../../include/pple/animation.hpp); tests in [animation_test.cpp](../../tests/animation_test.cpp) |
| Comfy Cakes: the Purble Chef's states, movies and tips | [cakes_chef.cpp](cakes_chef.cpp): `cakes_load_chef`, `cakes_chef_show`, `cakes_chef_advance`, `cakes_draw_chef` |
| Resource resolution, image decoding, sprite XML, file/PPM I/O, localiser | [resources.hpp](resources.hpp), [resources.cpp](resources.cpp): `Bundle`, `resolve_sprite`, `decode_payload`, `capture` |
| JPEG decoding for the browser build (libjpeg port; the desktop uses SDL3_image) | [jpeg.cpp](jpeg.cpp): `jpeg_decode_rgba` |
| Sheet texture cache, colour/alpha composition, coverage, the double-DPI (x2) resources | [textures.cpp](textures.cpp): `load_sheet_texture`, `load_sheet_surface`, `load_split_list`, `src_scale_for`, `clear_sheet_textures` |
| Scene XML draw list, labels, draw-list release | [scene.cpp](scene.cpp): `build_scene`, `append_label`, `release_draws` |
| Skeletal movie evaluation and part placement | [movie.cpp](movie.cpp): `build_movie`, `append_movie` |
| Individual image/sprite browser | [browser.cpp](browser.cpp): `load_current` |
| Audio manifest, PCM playback, shared cached Pairs/Cakes sounds | [audio.hpp](audio.hpp), [audio.cpp](audio.cpp): `initialise_pair_audio`, `initialise_cake_audio`, `play_click_sfx`, `play_cake_sfx`, `load_audio_pcm`, `set_sfx_audio_dir`; Cakes dispatch tests in [cakes_audio_regression.py](../../tests/cakes_audio_regression.py) |
| Fonts and diagnostic HUD | [hud.cpp](hud.cpp): `font_for`, `rebuild_hud`, `refresh_hud` |
| Pairs setup orchestration, deal/reset, solve/click hooks, scene visibility | [pairs_setup.cpp](pairs_setup.cpp): `build_pairs` |
| Per-level card back, jar points/coins, arc and burst configuration, animation reset | [level_art.cpp](level_art.cpp): `build_pair_level_art` |
| Card face/back/outline/blank/spark/glow/sneak-icon cache and time-bar assets | [card_assets.cpp](card_assets.cpp): `build_pair_card_assets` |
| Progress indicators, count-aware sneak tokens, sneak movie loading | [pair_hud.cpp](pair_hud.cpp): `build_pair_hud` |
| Cached card/effect draw lists (including the deal-in's growing cards), jar layering, timer/token/Professor draws, the hovered card's outline and glow, the sneak card's eye | [pairs_render.cpp](pairs_render.cpp): `refresh_cards`, `timed_refresh` |
| Flips, the deal-in, match sparks, flying jar coins, burst start/cache | [pairs_effects.cpp](pairs_effects.cpp): `advance_flips`, `advance_card_fx`, `start_deal`, `advance_deal`, `finish_deal`, `spawn_arcs` |
| Board completion, score/rank, settings save | [pairs_round.cpp](pairs_round.cpp): `finish_board_end`, `report_round_end` |
| Resumable round: state XML, RGMH container, board thumbnail | [pairs_state.cpp](pairs_state.cpp): `read_pair_state`, `load_pair_state`, `write_pair_state`, `render_board_thumbnail` |
| Professor state machine, tips, transitions | [professor.cpp](professor.cpp): `professor_show`, `professor_display_tip`, `professor_tip_closed`, `professor_advance`, `professor_round_end`, `professor_intro_begin`, `professor_level_tip` |
| Shared white/red/green nine-slice panels, atlas frame UVs, tip text and fonts | [tips.cpp](tips.cpp): `load_tips`, `load_tip_slice`, `load_tip_style`, `show_tip`, `append_panel`, `append_text`, `face_font`, `rebuild_overlay`; three-game seam/focus checks in [ui_panels_regression.py](../../tests/ui_panels_regression.py) |
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

## The browser build's seams

One source list builds both viewers (see [CMakeLists.txt](../CMakeLists.txt)): `pple-view` for the
desktop and `pple-view-web` for Emscripten. Six guarded places differ, and nothing else:

* audio decode — [wma.cpp](../wma.cpp) (Media Foundation) or [wma_stub.cpp](../wma_stub.cpp)
  (silent), with `--audio-dir` pointing at the WAV pack `pple-res audio-pack` writes; the reader
  is `pple::wav_read` in [wav.cpp](../wav.cpp);
* JPEG sheets — SDL3_image, or [jpeg.cpp](jpeg.cpp) through Emscripten's libjpeg port
  (`PPLE_HAVE_SDL_IMAGE` in [resources.cpp](resources.cpp) and [textures.cpp](textures.cpp));
* the frame loop — `while (loop.running) frame_step()` or `emscripten_set_main_loop` calling
  `frame_step` ([loop.cpp](loop.cpp));
* the entry point — a stack `App` or a long-lived one ([view_main.cpp](../view_main.cpp));
* the keyboard element hint ([app.cpp](app.cpp));
* `LoopState` in [app.hpp](app.hpp), the loop's clocks, which the browser cannot keep in a local.

`App::run_loop` is setup and dispatch, `frame_step` is one frame, and `finish_loop` releases the
session — the browser reaches the last of those from the main-loop cancel path, not from a
`return`. Do not move session state back into `frame_step` locals.

The page itself is [web/shell.html](../web/shell.html): it asks the player for the game DLLs (and
an optional sound pack), writes them into the Emscripten file system and calls `main` with
`Module.callMain`, which supplies `argv[0]` itself — the page passes `argv[1:]`. `?auto=1&assets=`
serves a copy, `?shot=1` paints one `--shot` frame on the canvas, and `window.ppleState` is what
the headless browser check waits on. The fonts under [web/fonts](../web/fonts) are OFL and
committed; the game files never are.

## The double-DPI (x2) art

`App::sheet_dpi` selects which resources every sheet request is answered from: the
shipped 1x pair, or the game's own x2 set (`--hidpi`, `--no-hidpi`, or automatic above
a 1.5x window scale). `load_sheet_surface` is the only place that decides it — a sheet
`SPLITLIST.XML` lists becomes its four `<stem>x2<page>` pages assembled into one canvas,
anything else becomes its `"x2"`-suffixed sibling — and it returns the multiplier the
caller's source rectangles need. That multiplier is what `RenderTexture` uses in the
frame loop (`src_scale_for`), so a draw item keeps its 1x `dst` and its 1x source rect:
only the texels read change, which is what makes the two modes lay out identically.

Nothing else may hold a sheet texture across a mode change. `set_sheet_dpi`
([loop.cpp](loop.cpp)) releases the draw lists while the cache still holds their borrowed
textures, drops the Pairs scene layer and the tip styles, clears the cache, and rebuilds
the current screen through `rebuild_current_screen`. The frame loop calls it only while
no tip and no end-of-round panel is up, because those borrow sheet textures of their own.
`texture_src_scale` and `sheet_src_scale` are the two views of the same fact, by texture
pointer and by resolved resource name.

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
is the only place the state machine moves. Do not add movie or sheet loads to it. Movie
keyframes are steps, not tweens: `pple::movie_evaluate` gives every part the value of the last
`<Frame num=>` at or before the frame being drawn, because the engine applies a key only on its
own frame number. The blink, mouth and exit-blur tracks depend on that — several copies of one
sprite are authored to snap between alpha 0 and 100, so interpolating them mixes the open eye with
the closed blue lid (no pupil, and the room tints the soft rim) and spreads the exit's one-frame
blur flash over the whole movie.

The deal-in runs on the same 30 Hz tick, before the board's own step: `advance_deal` moves each
card's `Card+0x14` counter, `refresh_cards` draws a card that has not started yet as nothing and
one that is still growing as its back art scaled around the cell, and `finish_deal` is both the
end of the deal and the place `Board::Update`'s `DisplayIntro` is reproduced, so `intro_wanted`
is consumed there rather than at `start_game`. `click_scene`'s card hit test stops the deal the
way `Card::DoCardClick` does; the screen's own buttons are not gated by it.

`click_scene` is also the only place a card click turns into rules: it refuses a card that is still
flipping, hands a face-up card to `ConcentrationBoard::click`'s take-back path (`Card::HandleEvent`'s
`FaceAnimation` branch, which also clears the card waiting for a partner and plays nothing), and
otherwise passes the reveal on. Mismatch timing belongs to the rules, not here: the board keeps one
`Card+0x3C` countdown per cell, so the loop redraws on `face_revision()` rather than on a single
lockout value, and a pair that is already counting down is not postponed by a newer one. The time bar
is part of the same draw list and is built from the board's clock, so `App::time_bar_state()`
([pairs_render.cpp](pairs_render.cpp)) is what says whether a tick moved it -- the segment index and
the 900-tick red threshold -- and `refresh_cards` draws from that one function. Slice `i` is drawn
with frame `i` of its own template: `ConUILoader::CreateTimeSlice` (`0x10008C640`) stores the slice
index in the sprite's frame field (`[r9+180h]`), and the template's 226 one-pixel frames are the
bar's vertical profile, so one frame for every slice turns the tube into a flat slab
(`s26-timer-compare.png`).

The sneak bonus's mark is a card node, not a screen one: `Card::InitUI` (`0x10007BDFA`) builds
`Card+0xA8` ("SneakIcon") from `SneakPeakMiniIcon.xml` as a child of the card's Base, so it
inherits the card's scale, and `Card::DoSneakIcon` (`0x10007DE60`) shows it while `Card+0x18` --
set on one random card by `Board::SelectSneakCard` (`0x10008B348`, exposed as
`ConcentrationBoard::sneak_card`) -- is set and `Card+0x568` is below 2. It is the golden eye the
shipped game puts on that card, drawn at the card's Base + `(1, 6) * scale` from the sprite's
`(-1,-6)` registration.

## Hover

The engine's focus follows the mouse, so "hover" and "focus" are one thing:
`UserInterface::ProcessMouseMove` (`0x100098498`) hit-tests the node under the cursor and calls
`SetFocus` (`0x1000992E0`), which sends `Event_FocusLost` / `Event_FocusGot` (`0x12` / `0x11`,
`Event_FocusLost::Create` `0x100094ADC` and `Event_FocusGot::Create` `0x100094AA8`) to the old and
the new node. Exactly one node is focused, which is why `update_hover` sets all three targets from
one hit test and why `click_scene`'s order -- the screen's buttons, then the cards, then the sneak
tokens -- is also the hover order. The loop routes **every** screen's motion through it
(`SDL_EVENT_MOUSE_MOTION`, and `SDL_EVENT_WINDOW_MOUSE_LEAVE` clears it), which is what makes the
chooser's and the score panel's buttons highlight as well as the board's.

The art each kind of node draws when it is focused:

* a card shows `highlite_outline.xml` (`Card+0xD0`), and `card_glow.xml` (`Card+0xC8`) only while
  it is face up -- `Card::HandleEvent`'s FocusGot case `0x10007C46F` and `Card::OnReveal`
  `0x10007F849`. The outline's visible flag is written *only* by FocusGot and FocusLost, so the
  outline is not a "card waiting for its partner" marker: it follows the pointer, which is also
  why `Card::UpdateFlipForward` / `UpdateFlipBackward` / `OnReveal` / `OnHide` only rescale and
  reposition it. The glow is additive: `Card::InitUI` sets its material to 2 at `0x10007BCE9`, and
  `PurblePlaceUILoader::SetNodeMaterial` (`0x1000730B0`) maps `<Material>alpha</Material>` to 1 and
  `<Material>additive</Material>` to 2 into `node+0x168` -- so `DrawItem::additive` exists for it;
* a `NodeButton` switches to its second `<Animation>` -- `NodeButton::HandleEvent` `0x1000AAC92`.
  That covers the menu's four buildings, the screen's `ReturnToMainMenu` (swapped in place at the
  index `build_scene` recorded in `scene_button_at`), and the sneak peek tokens, which
  `ConUILoader::LoadSneakPeekToken` (`0x10008C48C`) builds as buttons named "Button" with
  `SneakPeakButton.xml`'s idle eye on `concentrationSheet29` and its hover/pressed eyes on
  `sheet12`.

The hover art is the animation's **last frame**, not its first. `NodeButton::Load`
(`0x1000AAFF0`) builds a frame animation for the second and third `<Animation>` of a three-source
button -- `Animator_LinearAbsolute` from frame 0 to `<FrameCount>-1`, whose duration
`Animator_LinearAbsolute::GetPercentChange` (`0x1000A3204`) scales by 30 ticks a second off
`/ButtonFPS` (default 30) -- and `NodeButton::HandleEvent`'s FocusGot case (`0x1000AA985`) resumes
it, so a focused button settles on the last frame. `ReturnToMainMenu.xml` is the only one of these
buttons with a multi-frame hover art: frame 0 is a near-copy of the idle art and frame 1 is the
white highlight, so drawing frame 0 made the whole hover look dead (`s26-hover-btn-2way.png`).
`pairs_setup.cpp` and `menu.cpp` both go through `pple::sprite_frame_rect(geom, frames - 1)` for
that reason.

The same FocusGot path carries the engine's *simple* tips for two objects, and the
reconstruction now draws both: `SneakPeakArea::HandleEvent`'s MouseEnter case
(`0x100084D3C`) shows `SneakPeak` over a token and `Card::HandleEvent`'s MouseDown case shows
`BlankCard` for a matched cell, both at `615,125` from `CONTipManager::ShowSimpleTip`
(`0x100085C8C`). `update_hover_tip` (called from `update_hover` **and** from the 30 Hz tick,
because a match can turn the card under a still pointer into a BlankCard) owns them;
`hover_simple_expired` latches an expired bubble so the next tick does not raise it again, and
`professor_tip_closed` returns early for a simple tip so closing one never moves the Professor.

A tip's 9-slice comes from its own `<Type>`: `tip_style_for` maps `Intro`/`Error`/everything else
to the White/Red/Green `Tips\Styles\*` documents, and `green_style()` is what the difficulty
chooser and the end-of-round panel keep using.


## Comfy Cakes

`cakes_redraw` restores an immutable room draw/name cache before updating transport, then
composites both TVs above papers and gifts. The target sits between ScreenSprite and GlassSprite.
Transport draw indices always refer to the original cached room, never the reordered frame.
`cakes_transport_draw` hides ScreenSlot score marks during delivery and restores earned marks
afterward; score/flash clocks continue independently. Both DPI modes are covered by
[cakes_animation_regression.py](../../tests/cakes_animation_regression.py) and
[cakes_focus_readout_regression.py](../../tests/cakes_focus_readout_regression.py).

Final-touch rules and glaze-colour identity belong to [cakes.cpp](../cakes.cpp) and
[cakes.hpp](../../include/pple/cakes.hpp). `cakes_load_model` loads `GlazeLayers.xml` once;
`cakes_draw_cake` resolves each layer through the model's colour/shape-aware definition for both
TV and belt drawing. No new per-tick asset loading or room rebuilding. The
[cakes_regression.py](../../tests/cakes_regression.py) matrix now has 27 runs, including normal
Advanced glaze/sugar clicks in both DPI modes and regenerated TV finish-art checks.
Machine drawing anchors are keyed by the scene container's actual name in `build_cakes`, not
by a button prefix (`Final*` belongs to `FinalTouchTool`). The Cakes integration/UI suites also
assert on-screen final-machine action/base placement on direct and menu-started Advanced rounds.

`cakes_screen.cpp` owns the second game's room.  Its document is not a pure layout document:
`ComfyCakesGame::InitGameUI` (`0x10005D018`) looks each element up by name and creates the node
itself, so the module carries the engine's **creation order** as its draw order and adds the few
nodes the engine clones by count:

| Engine | Draw order | Module |
|---|---|---|
| `ConveyorBelt::Init` (`0x1000623A0`) | `/LeftBeltSprite`, **seven** `/MainBeltSprite` copies 99 px apart (the seventh switched to the belt's second `<Animation>` at `0x100062543`), `/BackWall` | belt tiles |
| `InitGameUI` (`0x10005DE17`) | `/ScreenSlot` **eight** times, four across at 20 px then a second row 30 px down | screen slots |
| `InitGameUI` (`0x10005DB51` / `0x10005DBAE`) | `TimerBackSprite` at (-10,325) and `TimerBarSprite` at (12,346), built by name rather than by the document | timer sprites |
| `InitGameUI` (`0x10005E0DB`) | the two Media Center header buttons and labels, only when `g_bMediaCenter == 1` | `--mce` keeps them, as on the Pairs screen |

The core loader resolves the rest.  A `<Desc>` that carries no node reference of its own is a
**widget**: `PurblePlaceUILoader::GetDescNode` (`0x100072E80`) follows a `<Desc>` whose text names
another document, and `SetWidgetPos` (`0x100072F20`) places the widget at the outer X/Y.  Comfy
Cakes' PurbleChef, CakeScreen, ConveyorBelt and the six Tools are all of that kind, and
`scene_load` qualifies every item a widget produces with `<widget element>/`, so
`ConveyorBelt/MainBeltSprite` is the name the engine itself uses.  `CakeScreen`'s
`<BackgroundSprite>` is a plain resource name inside the widget's `<Desc>`, which the generic
`<Background>` shorthand never reaches, so `walk_widget` emits it at the widget's own origin the
way `CakeScreen::Init` (`0x1000578DB`) does.

The viewport is the room's 800x496 design bounds, never the sprites' extents: the DroppingBox's
150x270 frame reaches x = 825 and the Media Center art starts at negative y, so both are clipped
exactly as the Pairs board's offscreen parts are.

The game rules (cake generation, the belt, judging, scoring) live in `pple::CakeModel`
(`src/cakes.cpp`); `cakes_play.cpp` only moves that state, logs what happened and draws it.
`build_cakes` resolves the room and records each tool button's `<Type>` (a LAYERS.xml entry) by
name; a click on one of them becomes `cakes_use_tool`, an arrow click `cakes_slide_left` /
`cakes_slide_right`.

**Each tool works on the belt slot its own machine stands over.**  `InitGameUI` hands the six
`<Tool>` elements their positions in document order (`0x10005DABC`: `Tool+60 = tools loaded so far
+ 2`), so CakePanTool slot 2, BatterTool 3, FillingTool 4, IcingTool 5, DecorationTool 6 and
FinalTouchTool 7 — the left-to-right order of the machines in the room.  `Tool::UseTool`
(`0x10007525C`) takes `ConveyorBelt::GetCake` at that slot, so `cakes_use_tool` does too, and the
cake is slid between machines: slot 1 is where a replacement enters, slot 8 is where a cake is
judged (ConveyorBelt::SlideRight harvests it at `0x100062D64`), and slot 0 is the chute a
replacement arrives on.  A button whose machine has no cake under it takes `Tool::UseTool`'s
refusal branch instead: two sounds, and `MachineUnusableShakeAnim.xml` shaken on the machine Base.

**A click that is accepted plays that machine's own animation**, and `cakes_tools.cpp` owns it.
The parts and their per-flavour animations come out of the tool documents
(`UI\COMFYCAKES\TOOLS\*.XML`); each part is a sprite with a position inside the tool's container
and a list of `<*Anim>` documents, one per flavour of that tool.  A `ResourceAnimation` is a list
of `<Animation>` blocks — `<Affects>` names the property, `<Type Name="LinearAbsolute">` carries
`<Start>`/`<End>` and `LinearRelative` a `<Count>`, with `<Time>` in seconds and
`<WaitForComplete/>` separating sequential groups. [animation.cpp](../animation.cpp) evaluates
parallel blocks before each barrier, relative accumulation and independent Position axes. An
`IntMessage` schedules an effect at its property crossing, not at the end of the whole animation.
**Correction to session 33:** the logical frame may exceed `FrameCount`, but
`NodeSprite::UpdateCurrentUVs` (`0x100094484`) clamps its UV index to `FrameCount - 1`.
Sampling neighboring sheet art was incorrect; the draw geometry now keeps the declared count.

The model's updated stack and the immutable pre-use stack are separate while a tool runs.
The batter machine draws **one** fill shape with the selected batter flavour. Pan and decoration
wait for their action cue, then integrate the recovered 60 Hz integer drop timers; icing starts
its stream and splat in sequence; filling runs tubes -> action -> splat; final touch waits for a
base rotation when changing selection. Fast input completes the previous tool/slide first.
Removal/replacement movies still approximate some original transitions; the pre-use stack prevents
unrelated layers disappearing while an effect is running.

The Chef, machines, belt and dispatch are animated per frame, so
`cakes_refresh` (which calls `build_cakes`, the expensive half) is only run on a click, a hover or
a rebuild, and `cakes_redraw` rewrites everything after the static room in place.  A hover still
rebuilds through `cakes_refresh`, so the rebuild re-appends the Chef, the TV's target cake and
the belt cakes and they survive the pointer moving over the room; the draw dump
(`cakes_dump_draws`) runs for every `cakes_refresh` and again for a `--shot` capture.

The room's `/HighlightSprite`s are **not focus art**, and `build_cakes` never draws them.  They are
`CCTipManager`'s instructional cue: a glow that blinks over the control the player should click
next.  Each is created hidden with its `HighlightAnim` stopped (`InitGameUI`
`0x10005D449`/`0x10005D5D5` and `0x10005D4FD`, `Tool::Init` `0x100074EDD`/`0x100074F8A`,
`CakeScreen::Init` `0x100057A9C`), and the only code that resumes one is the tip manager, beside a
tip that says what to do: `OnIdleTimerFired` resumes the pan tool's glow and the TV's with
`GamePlayIdleStart` ("Start your cake here!", `0x100058494`/`0x1000584AA`, the latter into
`CakeScreen::PlayHighlightAnimation` `0x100057C64`) and the slide-right glow
with `GamePlayCakePanIdle` ("Use the RIGHT ARROW", `0x1000584E0`), and `OnToolButtonClick` /
`OnArrowButtonClick` do the same (`0x100058742`, `0x100058659`).  `Tool::UseTool` stops the glow on
the click that uses the tool (`0x100075246`) and `Tool::HandleEvent` restarts it whenever its 2 s
four-pulse blink completes (`0x10007550A`), which no focus art does.  A tool button's or belt
arrow's own hover is `<Animation>` 1 of its three, which `cakes_append` draws from
`cakes_hover_name`.  The instructional cue itself is not modelled, so no glow is drawn at all.

Both slides move each cake exactly one slot: left ascends, right descends, which is
`ConveyorBelt::SlideLeft`'s own `for (v4 = 1; v4 < v3; ++v4)` order.  Ascending on the left slide
matters because slot `p-1` is filled only after it has already been tested — the other way round a
cake cascades to the end of the line in one slide. [cakes_transport.cpp](cakes_transport.cpp)
retains the actual visual positions: initial/restored placement uses `98*p - 66`, while animated
moves use the shipped **92 px / 0.5 s**, without snapping an extra 6 px on completion. The two-axis
chute motion uses its resource. Belt tiles retain their static draw indices and update in place.
Wrapping props have no idle draw owner; correct cakes fade under BoxDrop, then CakeBox travels
on its 1.4 s dispatch path and the delivery sign animates. Wrong cakes use trash/red-light/readout
animations. Pending shipments queue, rather than replacing a running gift; new rounds cancel all
clocks and queued work. Room rebuilds replace node descriptions, not active clocks or positions.

After a correct cake, the next TV order is generated and immediate visibly identical orders are
retried. Wrong cakes retain their order. Per-order move minima accumulate for scoring. Changing
orders on untimed Intermediate/Advanced is a requested usability extension: the original AddCake
regenerates after a correct cake only on Beginner or time-attack rounds.

`cakes_chef.cpp` owns the Purble Chef.  His six movies are parsed once and keyed by state, and
`cakes_chef_show` pushes a state plus its tip onto a one-deep queue, entering him first when he is
hidden — which is `PurbleChef::Show`'s own `OnEnterState(2)` branch.  `cakes_chef_advance` runs on
the same 30 Hz tick as the deal-in and the jar, returns whether his visible frame changed (which
is what tells the frame loop to redraw him), and falls back to the looping Idle when a one-shot
movie ends.  His tips come from `COMFYCAKESTIPS.XML`, which `load_tips` merges into `tip_defs`
beside `TIPS.XML`; it takes the document as an argument and remembers which documents it has read.


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

### Comfy Cakes layout/playability regressions (session 34)

- [cakes_test.cpp](../../tests/cakes_test.cpp): CTest model test, 3000 generated cakes,
  accessible ingredients by difficulty, single-count height and named-widget ownership.
- [cakes_regression.py](../../tests/cakes_regression.py): 23 local-asset viewer runs,
  difficulty layouts/hot-spots, idle effect visibility, 80% TV target, wax paper, normal
  clicks and solves in both DPI modes, menu roundtrip, SDL keyboard switches/restarts,
  and Pairs solves. Arguments: build directory, sample directory, output trace directory.
- The generic scene loader preserves `SceneItem::widget_name` across appearance resolution;
  Comfy Cakes uses that owner to hide/shift only the intended tool's children.
- Machine parts belong to the dynamic draw tail even when idle, not both static/dynamic layers.
  `cakes_new_game` cancels the previous tool animation/shake before changing difficulty.

### Comfy Cakes animation regressions (session 35)

- [animation_test.cpp](../../tests/animation_test.cpp): pure-core timeline barriers, parallel
  blocks, two-axis Position, frame cues, reverse evaluation and integer 60 Hz drops.
- [cakes_animation_regression.py](../../tests/cakes_animation_regression.py): 42 normal-click
  captures of all six tools, one selected batter shape, clamped single-frame body art, belt
  motion/roller frames, wrapping, gift travel, trash, active-shipping hover and resets, both DPIs.
- [cakes_regression.py](../../tests/cakes_regression.py) also verifies that each successful order
  changes visibly, all solves still win, and no invalid machine sprite state is requested.
- Original `draw` diagnostic lines retain their format for existing coverage tools; separate
  `source` lines expose UVs/alpha and `cakes clocks` exposes active transport clocks.

### Comfy Cakes UI and geometry regressions (session 36)

- [overlays.cpp](overlays.cpp) owns the shared chooser/result draws. `App::picker_cakes` keeps
  the chooser destination across hover/DPI rebuilds; [input.cpp](input.cpp) dispatches the choice
  and routes modal result input before room controls. New Game/menu transitions release old panels.
- [cakes_screen.cpp](cakes_screen.cpp) qualifies widget keys once for TV frame/screen order.
  [cakes_transport.cpp](cakes_transport.cpp) keeps the dispatch platform
  and right-belt frames on shipping phases instead of the main-belt/chute clock.
- [cakes.hpp](../../include/pple/cakes.hpp) retains each shaped layer's height delta; model mutation,
  cake drawing and tool endpoints use `CakeLayerDef::delta_for(shape)`.
- [cakes_ui_regression.py](../../tests/cakes_ui_regression.py): 30 local-asset runs, both DPIs,
  all chooser options, modal win/loss actions, correct navigation captures, purple TV screen pixels,
  stable idle dispatch UVs and button-art-only arrow focus. Same three directory arguments as
  the other integration scripts. [cakes_test.cpp](../../tests/cakes_test.cpp) adds shaped-height
  and replacement assertions. Script navigation restarts frame layout before capture.

### Tool focus and shipping readout correction (session 37)

- This section's tool-halo centring is superseded by session 42: the tool and arrow
  `*HighlightSprite`s are the tip manager's instructional cue, not focus art, and are no longer
  drawn for the pointer at all. Button geometry was and is unchanged.
- [cakes_transport.cpp](cakes_transport.cpp) draws ScreenSlot **frames** 2 (permanent success) / 1
  (rejection flash) from its single sprite animation; empty slots have alpha 0. Its dedicated
  `CakeTransport::readout_time/readout_slot` start in `cakes_judge` and reset in `cakes_new_game`
  ([cakes_play.cpp](cakes_play.cpp)), independently of the gift dispatch queue. Hover rebuilds keep
  those clocks. This corrects the prior readout claim: selecting animation 1/2 fell back to frame 0.
- [cakes_focus_readout_regression.py](../../tests/cakes_focus_readout_regression.py) covers every
  visible tool group on all three difficulties, success, rejection, expiration, queued gifts,
  hover/reset persistence and final readouts across both rows in both DPI modes. Arguments:
  `BUILD_DIR SAMPLE_DIR TRACE_DIR`. Its focus check is now "the hovered button draws its own second
  `<Animation>` and no glow" (session 42).

### Cake/conveyor alignment correction (session 38)

- [cakes_transport.cpp](cakes_transport.cpp) fits each slide's authored axis displacement to the
  fixed destination slot, preserving its timing with no endpoint snap. `CakeBeltMotion::scale_x/y`
  in [cakes_transport.hpp](cakes_transport.hpp) own this per-slide adjustment; raw chute staging
  remains separate. Keeping 92px endpoints on a 98px grid previously accumulated leftward drift.
- [cakes_belt_alignment_regression.py](../../tests/cakes_belt_alignment_regression.py): 36 local-asset
  captures, all difficulties/both DPIs, initial/repeated/reversed slides, moving hover and replacement
  chute-to-belt entry. Same `BUILD_DIR SAMPLE_DIR TRACE_DIR` arguments as other viewer tests.

### Purble Shop (session 40)

- [shop_screen.cpp](shop_screen.cpp) owns the third game's room and [shop_screen.hpp](shop_screen.hpp)
  its parsed documents. Purble Shop is internally **Build-a-Purble**; the room is not a plain
  layout either, so the module parses `UI\BuildAPurble\BuildAPurbleGame.xml`,
  `UI\BuildAPurble\Cabinet\Cabinet.xml` and one `UI\BuildAPurble\PurbleFaceTemplate*.xml` and draws
  the elements itself: background, mystery head, curtain at its own frame, spotlights, the cabinet's
  stones/shelves/one visible variant per colour/check boxes, the guess button, the history strip,
  the colour counts and both faces. `shop_append` is the one placement helper (registration and
  scale, `state` for a button's hover art, `frame` for the curtain, the numbers and the
  thermometer).
- The round's state is `pple::ShopModel` ([shop.hpp](../../include/pple/shop.hpp),
  [shop.cpp](../../src/shop.cpp)) and the viewer only moves and draws it: `shop_select` is
  `OnSetFeature`'s toggle plus the item's own `<SoundID>`, `shop_guess` is `MakeGuess` (counts,
  score, history, Beginner's check-box marks, the reveal and the end-of-round panel).
  `shop_refresh` = `build_shop` + the HUD; it runs on a click, a hover, a screen rebuild or a
  curtain phase boundary, never per animation tick.
- The screen is `Screen::Shop`: the menu's `BuildAPurbleButton` opens the shared chooser with
  `picker_shop` (the shop's own background and option text), the choice calls `start_shop`, and
  `ESC`, `ReturnToMainMenu` or the result panel's Main Menu leave through `game_to_menu`.
  `--shop [D]`, `--shop-seed N` and `--shop-dump` are the CLI hooks.
- Sounds go through [audio.cpp](audio.cpp)'s shared cache: `initialise_shop_audio` preloads the
  twelve room events and `audio_event_at(bundle, proto)` maps a template `<SoundID>` to its event
  key, so a picked item plays its own sound and nothing decodes twice.
- Regression: [shop_test.cpp](../../tests/shop_test.cpp) (CTest `shop-rules`) covers the
  comparison rule, the per-difficulty setup, the toggle, the score arithmetic and both end
  conditions; `tests/cakes_regression.py` still passes over the menu/chooser/loop changes.
- The history strip is `HistoryWindow+4` (`App::shop_history_first`): `shop_draw_history` draws
  from it and ghosts an arrow at alpha 32 exactly where `UpdateSlots` does, `shop_history_slide`
  is the arrows' own action (`LEFT_BUTTON`/`RIGHT_BUTTON`, older/newer), and a new entry parks the
  window at `count - 6`. Each visible item registers **two** hot-spots in creation order,
  the item first and its check box second. Input tests the entire list backwards, because `FaceFeatureButton::Init` (0x10006A434) creates the box after the item
  and `RenderManager::HitTestTopLevel` (0x10008F3D0) walks the render list backwards, so the
  topmost node wins; `shop_tag` is `FaceFeatureCheckBox::HandleEvent`'s 1 -> 3 -> 2 cycle
  (Intermediate and Advanced only). `--shop-guess "c0,..,c4;..."` judges selections through the
  same `shop_guess` a click reaches.
  Correction (section 49): testing only the first matching hot-spot handled each checkbox but
  let an earlier neighbour steal clicks on overlapping Advanced mouths. Full reverse ordering
  now agrees with drawing order while keeping checkboxes above their own item.
- [shop_flow.hpp](shop_flow.hpp) owns the SDL-free curtain/feedback timeline, reset only by
  `shop_new_game`. `App::shop_advance` updates a cached curtain source rectangle, rebuilding once
  at phase boundaries. Intro opens/holds/closes (0.5/3/0.5 s); guessing rests closed; win/loss opens
  in 0.5 s on the real answer. The answer remains until a click opens the result panel.
  Selection uses one slot per shelf, ghosts its cabinet item to the authored alpha 128 endpoint,
  and hides stale counts. Checkbox tags are notes, not selected parts. Model and flow tests live
  in [shop_test.cpp](../../tests/shop_test.cpp); normal-click/pixel checks in both DPI modes are
  [shop_regression.py](../../tests/shop_regression.py).
  Not implemented yet: the stage intro, win/inflate animation, the game's own tips and save files.
  Item alpha currently snaps to its endpoint rather than interpolating over 0.2 s; the HUD
  guidance and persistent answer/click-to-continue are reconstruction usability extensions.

### Comfy Cakes hover correction (session 42)

- `build_cakes` no longer draws any `/HighlightSprite`. The tool, both arrow and the TV glow are
  `CCTipManager`'s instructional cue (see the Comfy Cakes section above); a hovered tool button or
  belt arrow shows only its own `<Animation>` 1 through `cakes_append`/`cakes_hover_name`.
- [cakes_focus_readout_regression.py](../../tests/cakes_focus_readout_regression.py) (**54 runs**)
  and [cakes_ui_regression.py](../../tests/cakes_ui_regression.py) (**30 runs**) now assert the
  button-art-only hover and the absence of every glow in both DPI modes;
  [cakes_animation_regression.py](../../tests/cakes_animation_regression.py) (**42**) and
  [cakes_belt_alignment_regression.py](../../tests/cakes_belt_alignment_regression.py) (**36**)
  still pass over the change.

Keep this map current. Add behavior to the owning module, not the entry point or
a catch-all helper. Split a module further only at a cohesive boundary; keep state
ownership and the relevant regression commands explicit. Functional changes and
structural moves should be separate milestones.
