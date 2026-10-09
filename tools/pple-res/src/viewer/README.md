# Viewer module map

Start here, then search the relevant module. Do not read the whole viewer to change
one feature. All files here use the internal `pple::viewer` namespace; command-line
flags and the core library's public interfaces are unchanged.

## Where to look

| Responsibility | File and entry points |
|---|---|
| Process entry | [view_main.cpp](../view_main.cpp): constructs `App`, calls `run` |
| Arguments, DLL/MUI loading, SDL setup, initial screen | [app.cpp](app.cpp): `App::run` |
| Events, 30 Hz updates, frame drawing, scripts, captures, benchmarks, teardown | [loop.cpp](loop.cpp): `run_loop`, `set_sheet_dpi`, `rebuild_current_screen` |
| Mouse/script hit testing, hover (focus) and screen navigation | [input.cpp](input.cpp): `click_scene`, `update_hover` |
| Menu, loading screen, drifting clouds | [menu.cpp](menu.cpp): `build_menu`, `build_loading`, `load_menu_clouds` |
| Comfy Cakes: the room, its draw order, the engine's own clones | [cakes_screen.cpp](cakes_screen.cpp): `build_cakes`, `cakes_append`, `start_cakes`, `cakes_dump_draws` |
| Comfy Cakes: the round's state over the SDL-free model | [cakes_play.cpp](cakes_play.cpp): `cakes_new_game`, `cakes_use_tool`, `cakes_slide_left`, `cakes_judge`, `cakes_compute_score`, `cakes_draw_cake`, `cakes_refresh`, `cakes_solve` |
| Comfy Cakes: the Purble Chef's states, movies and tips | [cakes_chef.cpp](cakes_chef.cpp): `cakes_load_chef`, `cakes_chef_show`, `cakes_chef_advance`, `cakes_draw_chef` |
| Resource resolution, image decoding, sprite XML, file/PPM I/O, localiser | [resources.hpp](resources.hpp), [resources.cpp](resources.cpp): `Bundle`, `resolve_sprite`, `decode_payload`, `capture` |
| Sheet texture cache, colour/alpha composition, coverage, the double-DPI (x2) resources | [textures.cpp](textures.cpp): `load_sheet_texture`, `load_sheet_surface`, `load_split_list`, `src_scale_for`, `clear_sheet_textures` |
| Scene XML draw list, labels, draw-list release | [scene.cpp](scene.cpp): `build_scene`, `append_label`, `release_draws` |
| Skeletal movie evaluation and part placement | [movie.cpp](movie.cpp): `build_movie`, `append_movie` |
| Individual image/sprite browser | [browser.cpp](browser.cpp): `load_current` |
| Audio manifest, PCM playback, cached Pairs sounds | [audio.hpp](audio.hpp), [audio.cpp](audio.cpp): `initialise_pair_audio`, `play_click_sfx` |
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

The game rules (cake generation, the belt, judging, scoring) are **not** in this milestone; the
screen is reachable, drawn and hoverable, and a tool or belt click is reported.

The game rules live in `pple::CakeModel` (`src/cakes.cpp`), not here: the viewer only moves its
state and logs what happened.  `build_cakes` resolves the room and records each tool button's
`<Type>` (a LAYERS.xml entry) by name; `cakes_play.cpp` turns a click on one of them into
`use_tool`, turns an arrow click into `slide_left` / `slide_right`, and judges the cake that
leaves slot 0 with `App::cakes_judge`.  A hover rebuilds the whole screen exactly as the menu
does — but through `cakes_refresh`, so the rebuild re-appends the Chef, the TV's target cake and
the belt cakes and they survive the pointer moving over the room; the draw dump
(`cakes_dump_draws`) only runs for a `--shot` capture.

The room's `/HighlightSprite`s are **focus art**: the engine creates each one hidden and shows it
while the button it belongs to has the focus (`InitGameUI` `0x10005D449`/`0x10005D5D5`,
`Tool::Init` `0x100074EDD`, `CakeScreen::Init` `0x100057A9C` all clear the node's visible flag at
+64 and stop the 0.25 s `HighlightAnim` fade-in), so `build_cakes` draws one only for the control
under the pointer — a tool's for its own buttons, the slide highlight for its own button, and the
TV's never at rest.

Both slides move each cake exactly one slot: left ascends, right descends, which is
`ConveyorBelt::SlideLeft`'s own `for (v4 = 1; v4 < v3; ++v4)` order.  Ascending on the left slide
matters because slot `p-1` is filled only after it has already been tested — the other way round a
cake cascades to the end of the line in one slide.

`cakes_chef.cpp` owns the Purble Chef.  His six movies are parsed once and keyed by state, and
`cakes_chef_show` pushes a state plus its tip onto a one-deep queue, entering him first when he is
hidden — which is `PurbleChef::Show`'s own `OnEnterState(2)` branch.  `cakes_chef_advance` runs on
the same 30 Hz tick as the deal-in and the jar, and falls back to the looping Idle when a one-shot
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

Keep this map current. Add behavior to the owning module, not the entry point or
a catch-all helper. Split a module further only at a cohesive boundary; keep state
ownership and the relevant regression commands explicit. Functional changes and
structural moves should be separate milestones.
