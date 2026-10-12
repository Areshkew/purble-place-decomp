# pple-res — resource loader, viewer and data-model inventory for Purble Place

C++20. pple_core is standard-library only; pple_audio adds Media Foundation for WMA, and the
viewer adds SDL3 (plus SDL3_image for JPEG and SDL3_ttf for the HUD).

```
cmake -S tools/pple-res -B build/pple-res -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/pple-res
```

## How to run it

The asset DLLs and the `.mui` are not part of this repository; they live in the game's working
copy at `C:\re\samples\purbleplace`. Pass them as arguments, in this order: `PurblePlace.dll`,
`PurblePlace2.dll`, `CardGames.dll`. `build\pple-res\` already has `SDL3.dll`,
`SDL3_image.dll` and `SDL3_ttf.dll` beside the executables, so run them from there or by path.

```powershell
$d = "C:\re\samples\purbleplace"
$m = "$d\en-US\PurblePlace.exe.mui"
$game = @("$d\PurblePlace.dll", "$d\PurblePlace2.dll", "$d\CardGames.dll")

# The real main menu — click the bell house, choose a difficulty, and the Professor
# walks in and shows his two "How To Play" bubbles before the board is yours.
.\build\pple-res\pple-view.exe @game --menu --mui $m

# Straight to the in-canvas Select Difficulty screen, with the Professor waiting.
.\build\pple-res\pple-view.exe @game --difficulty --mui $m

# Straight into the game, no menu and no intro (the analysis hook).
.\build\pple-res\pple-view.exe @game --scene "UI\CONCENTRATION\CONCENTRATIONGAME.XML" --pairs "Beginner,0" --mui $m

# The same board with the Professor's entrance and his intro bubbles.
.\build\pple-res\pple-view.exe @game --scene "UI\CONCENTRATION\CONCENTRATIONGAME.XML" --pairs "Beginner,0" --intro --mui $m

# Intermediate is the first timed difficulty, so it is the one that shows the time bar.
.\build\pple-res\pple-view.exe @game --scene "UI\CONCENTRATION\CONCENTRATIONGAME.XML" --pairs "Intermediate,0" --mui $m

# The second game: the Comfy Cakes room, also reachable from the menu's Comfy Cakes house.
.\build\pple-res\pple-view.exe @game --cakes --mui $m
```

Controls: **left click** a card to flip it, or a choice button to pick a difficulty and start the
next round; **click a card that is already face up** to turn it back down (`Card::HandleEvent`'s
`FaceAnimation` path, which also clears the card waiting for a partner); **click anywhere** to
advance or dismiss the Professor's tip; **N** or **R** for a new round, **ESC** to go back to the
menu (and to quit from the menu), **SPACE** to toggle whole-pixel scaling, **S** to write the
current image as a PPM. `--mute` silences the sound effects. `--mui`
is optional; without it the labels fall back to the English text embedded in the XML, which is why
the title then renders in Comic Sans MS rather than Segoe UI. The difficulty chooser and the
end-of-round panel keep working without it: they carry the shipped `.mui` wording as their
fallback.

On the Comfy Cakes screen: **left click** `ReturnToMainMenu` (or **ESC**) leaves, a **tool button**
or a **belt arrow** builds the cake or slides the belt, and the pointer's focus follows the mouse
exactly as it does on the Pairs board — a focused tool button or belt arrow draws its own second
`<Animation>`, and the target cake on the TV and the cake on the belt stay on screen while the
pointer moves. Each tool works on
the belt slot its own machine stands over, so the cake is slid from machine to machine: a button
whose machine has no cake under it shakes and refuses, and a button that has one plays that
machine's own animation before the ingredient appears on the cake. The rules, the machines, the
Chef and the score are in; see section 42 of `D:\Development\re\notes\purbleplace.md` for what the
room checks and what it still does not do. The glow the machines' `*HighlightSprite`s draw is the
game's instructional cue, not a hover, and is not modelled.

Headless equivalents that need no window (useful for checking a change):

```powershell
# The rules, with no SDL at all.
.\build\pple-res\pple-res.exe pairs "$d\PurblePlace2.dll" --autoplay

# A whole round through the viewer's own click handler, then one frame to a file.
.\build\pple-res\pple-view.exe @game --scene "UI\CONCENTRATION\CONCENTRATIONGAME.XML" --pairs "Advanced,0" --pairs-solve --mute --shot out.ppm

# The menu, one frame, no interaction.
.\build\pple-view.exe @game --menu --mui $m --mute --shot menu.ppm
```

`--shot` writes a PPM; `C:\re\traces\cpp\ppm2png.py` converts one to PNG, because image viewing
here does not take a PPM.

## Targets

| Target | Depends on | Purpose |
|---|---|---|
| `pple_core` | — | static library: PE resources, CAB/MSZIP, DEFLATE, PNG, XML, SHA-256 |
| `pple_audio` | Media Foundation (Windows only) | WMA/ASF decoding to 16-bit PCM |
| `pple-res` | `pple_core` | command-line inspector and extractor |
| `pple-xml` | `pple_core` | XML data-model inventory and resource-reference resolution |
| `pple-view` | `pple_core` + SDL3 (+ SDL3_image, SDL3_ttf) | viewer: the real main menu, the playable Purble Pairs screen, the image browser, and sprite-node and movie rendering |

The viewer is skipped when SDL3 is absent. Roots are configurable:
`-DPPLE_SDL_ROOT=`, `-DPPLE_SDL_IMAGE_ROOT=`, `-DPPLE_SDL_TTF_ROOT=`. Matching runtimes
(`SDL3.dll`, `SDL3_image.dll`, `SDL3_ttf.dll`) are copied next to the executable.

## Resource resolution, as implemented

Reverse-engineered from `DllFileMgr::LoadResource` (`0x1000BBF00`) and
`ResourceTextureD3D::CreateTextureFromSheet` (`0x1000A83DC`) and then verified against the
whole data set:

1. `'/'` is folded to `'\'`.
2. The name is tried as given via `FindResourceW(h, name, L"DATA")`, searching every loaded
   module in order.
3. On failure, `.cab` is appended and the result is extracted through the Windows Cabinet API
   (`CabLoader`, `FDICopy`).
4. A `.sht` sheet name resolves to **two** resources, both reached by overwriting the
   three-character extension: `<stem>.jpg` is opened as the **colour** sheet and `<stem>.bmp`
   (which only exists as `<stem>.BMP.CAB`) as the **alpha** sheet.
   `ResourceTextureD3D::CreateTextureFromSheet` (`0x1000A83DC`) requires equal dimensions and
   composes `ARGB = (alpha_byte << 24) | (colour_dword & 0xFFFFFF)` into an `A8R8G8B8` texture
   of the next power of two above each dimension, filling only the top-left `w x h`.
5. Audio-style names resolve under `AUDIO\SFX\`.
6. A sheet listed in `SPLITLIST.XML` (11 entries, all `1024x1024`) is a **split** sheet. When
   `g_bDoubleDPI` is set it is drawn from up to four page resources named `<stem>x2<page+1>`
   (`Sheets\x.sht` -> `Sheets\x21.jpg` … `x24.jpg`); at normal DPI the plain pair is used.
   The `SPLITLIST.XML` size is the **virtual** sheet, i.e. the padded 1x texture; a page holds one
   UV quadrant of it at twice the pixels, so page `p` sits at `((p%2)*w, (p/2)*h)` of a canvas
   twice the virtual size. `Material::Render` (`0x1000A96E0`) picks the page by quadrant and reads
   it with `local UV = (u - half) * 2 * (1024 / pageDim)`, which is that placement for a full page
   and a top-left-aligned crop for a trimmed one. Rebuilding the canvas that way and halving it
   reproduces the 1x sheet to a mean absolute error of **1.7–3.8** on all five sheets checked
   (`C:\re\traces\cpp\hidpi\rebuild_x2.py`), against 77–111 for the wrong quadrant.
7. Every texture is uploaded at the **next power of two** above each dimension and the resource
   then reports *that* size: `ResourceTextureD3D::CreateTextureFromSource` (`0x1000A8F64`) runs
   `if ((w & -w) != w) { for (i = 1; i < w; i *= 2); w = i; }` on both axes and ends with
   `*a3 = w; *a4 = h;`. A node's `<SheetOffsetX>`/`<SourceWidth>` are therefore in padded-texture
   space, not in file space, which is the only way `<PlayArea>`'s `148,0 800x500` fits into an
   800x496 PNG (the texture is 1024x512). `load_sheet_texture` pads the surface the same way and
   leaves the padding transparent.

Measured over 840 XML resources: 3652 references, 3047 resolved — 2039 exact, **1008 by the
`.sht`→`.jpg` rewrite**, 147 by the `AUDIO\SFX\` prefix.

Where the Concentration data actually lives, because it is not where its own name suggests:
`CONCENTRATIONGAME.XML`, `CARDS.XML` and the `Loading.xml`/`PurblePlaceManager.xml` pair are in
`PurblePlace.dll`, while **every other `UI\Concentration\*.xml`** — `Sham.xml`, `ShamButton.xml`,
`CardBacks.xml`, `RANKS.XML`, the four `*BurstMovie.xml` files, the two emitters, the sparks and
all the `*_Points.xml` files — is in `PurblePlace2.dll`. The loader searches every loaded module,
so the viewer only cares that all three are passed. `pple-res list --grep` will not find them: its
name filter runs before the case fold. Use `pple-res find <pe>... <payload text>` instead.

## pple-res

| Subcommand | Purpose |
|---|---|
| `summary <pe>...` | PE header facts, section table, resource counts by type, payload-kind histogram |
| `list <pe> [--grep TEXT] [--limit N]` | Every resource with type, language, size, identified kind, name |
| `verify <pe>...` | Inflates **every** CAB resource, checking each block against `cbUncomp` and the total against `CFFILE.cbFile` |
| `extract <pe> <name> <out>` | Writes one resource; a CAB inflates to the inner asset unless `--container` |
| `find <pe>... <text> [--limit N]` | Case-insensitive search (ASCII and UTF-16LE) over every resource payload; prints resource, offset and context |
| `split <pe>...` | The `SPLITLIST.XML` table, with each split sheet's four `x2` page resources |
| `cards <pe>...` | The Concentration card deck (`UI\CONCENTRATION\CARDS.XML`) and the `<type>` -> pool mapping |
| `pairs <pe> [--difficulty D] [--level N] [--seed N] [--layout] [--autoplay] [--sneak-check] [--wild-early] [--spam-check]` | Builds a real Purble Pairs board from the shipped data; `--layout` prints every cell, `--autoplay` plays the whole round out, `--wild-early` spends the Wild Card on the first partner before the autoplay does (the regression test for the twin it clears), `--spam-check` is the fast-click regression: one click per tick, per-pair mismatch countdowns and the card waiting for a partner (17 assertions) |
| `strings <file.mui> [--id N] [--grep TEXT] [--limit N]` | Dumps the `RT_STRING` table by id |
| `muicheck <file.mui> <pe>...` | Compares every `\|id\|context\|English` reference in the XML resources against the MUI |
| `audio <pe>... [--list]` | `AUDIO.XML` events with their `File`/`Volume`/`Buffers`, and how each `File` resolves under `AUDIO\SFX\` |
| `asf <pe>... [--list]` | Every ASF/WMA resource: codec, channels, sample rate, bits, bitrate, duration, packet count |
| `wma <pe>... [--all] [--resource NAME --out FILE.wav]` | Decodes WMA through Media Foundation; `--all` checks the decoded length against the container's declared duration |
| `png <pe> <name> [out.ppm]` | Decodes one PNG resource, optional PPM dump |
| `pngscan <pe>...` | Decodes every PNG resource and reports formats and failures |
| `rgmh <file>` | Reports the `RGMH` save-file header by offset, with only checkable claims |
| `rgmhrt <in> [out]` | Reads an `RGMH` file and re-emits it from the parsed fields, then compares byte for byte |
| `consave <in> [out]` | `ConcentrationScoresAndSettings.xml`: parses the settings XML out of the container and re-emits **both** the XML and the container, then compares byte for byte |
| `sha256 <file>` | Hex digest for cross-checking extraction |

## pple-xml

```
pple-xml <pe>... [--refs] [--examples N] [--path SUBSTR] [--list]
```

Reports parse coverage, root elements, the element-path histogram with each path's attributes
and example values, and — with `--refs` — how every resource reference resolves.

## pple-view

```
pple-view <pe>... [--menu] [--difficulty] [--cakes] [--shop] [--resource NAME] [--sprite XML] [--scene XML] [--movie XML]
                  [--movie-name NAME] [--anchor X,Y] [--frame N] [--state N]
                  [--pairs [D,L,S]] [--pairs-reveal] [--pairs-click I,J] [--pairs-ticks N]
                  [--pairs-solve] [--pairs-save PATH] [--mce] [--no-container-offsets] [--mui PATH]
                  [--intro] [--no-intro] [--no-tips] [--deal] [--no-deal]
                  [--loading [MS]] [--hidpi] [--no-hidpi]
                  [--play NAME] [--volume N] [--mute]
                  [--script-click "x,y;x,y;move:x,y;wait:MS"] [--shot-delay MS] [--bench N] [--bench-build N]
                  [--shop-seed N] [--shop-dump]
                  [--list] [--shot out.ppm] [--save out.ppm] [--width N] [--height N]
                  [--font PATH] [--all]
```

`--menu` is the real main menu and the entry point: `PurblePlaceManager.xml` (which lives in
`PurblePlace.dll`) for the background, the `Foreground` sprite (which paints the four icons), the
title labels and the four `<NodeButton>` hot-spots, with each button's `<Animation>` 1 as the
hover art. Hover follows the pointer on every screen, not just the menu: the buildings, the
chooser's and the score panel's buttons, the in-game `ReturnToMainMenu`, the cards and the sneak
peek tokens all take their focus from the node under the mouse. Clicking the Purble Pairs hot-spot
opens the in-canvas **Select Difficulty** screen
(see below) and its three buttons start the game; the in-game `ReturnToMainMenu` button comes
back; `ESC` goes back too, and `N`/`R` start a new round at the same difficulty.
`--difficulty` starts on that chooser directly. `--mce` additionally draws the Media Center
build's `New Game` / `Exit Game` header buttons — the shipped executable *deletes* those five
nodes when `g_bMediaCenter == 0` (`ConcentrationGame::LoadGameUI` `0x10008122B`), so they are
absent by default, as they are in this build.  `--no-container-offsets` turns off the engine's
HUD re-parenting described below.

### Purble Shop

`--shop [Beginner|Intermediate|Advanced]` starts the third game on
`UI\BuildAPurble\BuildAPurbleGame.xml`; the menu's Purble Shop house reaches it the same way as
the other two (the manager's own Build-a-Purble state).  `--shop-seed N` picks the round's
template, its three variants per colour and feature, and its hidden face, and `--shop-dump` prints
every hot-spot of the room so a scripted run can aim at the cabinet.

Purble Shop is internally **Build-a-Purble**.  Its room is not a layout document either:
`BuildAPurbleGame::LoadGameUI` (`0x100050B54`) looks every element up by name and the cabinet and
the faces are widgets of their own, so [shop_screen.cpp](src/viewer/shop_screen.cpp) parses
`BuildAPurbleGame.xml`, `UI\BuildAPurble\Cabinet\Cabinet.xml` (`FeatureSet::Init` `0x10006BC50`)
and one of the six `UI\BuildAPurble\PurbleFaceTemplate*.xml` files (`Face::SetTemplate`
`0x100069444`) and draws:

* the room background (`UI\BuildAPurble\Background.png`, which already contains the cabinet panel
  and the colour-count panel), the curtain rod, the curtain at its own frame (7 is open, 15
  closed), the two spotlights and the dark **mystery head** that stands in for the hidden purble
  until the reveal;
* the cabinet: the colour stones of the current difficulty, that difficulty's shelves, and for
  every colour the **one visible variant** of each shelf (the round picks one of three variants per
  colour and feature) with its check box (empty, green check or red X);
* the **guess purble** the player assembles -- body, head and the picked feature sprites, in the
  template's own document order -- the guess button, the six history slots with their thermometer
  and numbers, and the colour counts.

The rules live in `pple::ShopModel` (`src/shop.cpp`): a round keeps 3, 4 or 5 colours and feature
slots, the player picks a colour per shelf, and `Face::Compare` (`0x100069F18`) scores the guess as
*right colour/right feature* and *right colour/wrong feature* over the hidden face's colour
multiset.  A won or lost round reveals the hidden purble, opens the curtain, plays the round's own
sounds and raises the same end-of-round panel the other two games use.


### Comfy Cakes

`--cakes` starts the second game on `UI\COMFYCAKES\COMFYCAKESGAME.XML`, and the menu's Comfy Cakes
house reaches it the same way the shipped build does (`PurblePlaceManager::Init` sets its state to
2 at `0x100071A00` / `0x100071A1A`).  `ReturnToMainMenu` and `ESC` come back to the menu.

The room is a scene load plus the engine's own draw order.  `ComfyCakesGame::InitGameUI`
(`0x10005D018`) creates every node itself rather than walking the document, so
[cakes_screen.cpp](src/viewer/cakes_screen.cpp) carries that order:

| Order | Nodes |
|---|---|
| 1-3 | `/Background`, `/RightBelt` + anim, `/ConveyorPlatform` + anim |
| 4 | `/ConveyorBelt`: `LeftBeltSprite`, **7 x** `MainBeltSprite` 99 px apart (the seventh on the belt's second `<Animation>`), `BackWall` — `ConveyorBelt::Init` `0x1000623A0` |
| 5 | `/CakeScreen`: `BackgroundSprite`, `ScreenSprite` (0,20), `GlassSprite`, `HighlightSprite` — `CakeScreen::Init` `0x1000577FC` |
| 6-13 | the two slide buttons and their highlights, `/RedLight`, `/CakeBox`, `/TrashCan`, `/ReturnToMainMenu` |
| 14 | `/Tools`, one per `<Tool>`: `HighlightSprite`, its `<ButtonTool>`s, then the subclass's own sprites |
| 15 | `TimerBackSprite` (-10,325) and `TimerBarSprite` (12,346), built by name at `0x10005DB51` / `0x10005DBAE` |
| 16-19 | `/ShippingSignBackground`, `/ShippingSign`, **8 x** `/ScreenSlot` (four across at 20 px, second row 30 px down), `/DroppingBox` |
| 20-21 | `/PurbleChef`, then the two Media Center header buttons and labels, which `--mce` keeps |

Current room logs: **43 / 48 / 50 static sprites**, **16 / 19 / 21 hot-spots**
(Beginner / Intermediate / Advanced), **0 unresolved**, in an 800x496 viewport. Machine bodies,
ingredient effects, wax paper and active gifts are drawn in the dynamic layer. Hiding the two
idle gift props supersedes session 34's 45 / 50 / 52 counts and the older 78-sprite room.  A `<Desc>` with no node
reference of its own — PurbleChef, CakeScreen, ConveyorBelt and each Tool — is a **widget**:
`GetDescNode` (`0x100072E80`) follows the document it names and `SetWidgetPos` (`0x100072F20`)
places the widget at the outer X/Y, which is what the core loader now does for every scene.

The rules — cake generation, the belt, judging, the score, the machines' animations and the Chef —
are in, through the click path and `--cakes-solve`.

### Comfy Cakes: the round

`--cakes [Beginner|Intermediate|Advanced]` starts a round directly (the analysis hook). The menu's
Comfy Cakes house now opens **Select Difficulty**, with cake-specific descriptions rather than
Pairs grid descriptions. `ReturnToMainMenu` and **ESC** leave. Wins and losses show Game Points,
Final Score, the moves bonus, and **New Game / Change Difficulty / Main Menu** buttons; the panel
is modal while dispatch animations continue behind it. This replaces the previous keyboard-only
round end. [cakes_play.cpp](src/viewer/cakes_play.cpp) owns play state over the SDL-free
[model](src/cakes.cpp); [overlays.cpp](src/viewer/overlays.cpp) draws the shared chooser/result UI:

| Piece | Engine | Model |
|---|---|---|
| the difficulty variables | `COMFYCAKESVARS.XML`, bound by `TweakVars::Bind` in `ComfyCakesGame::Init` (`0x100059F9C`) | `CakeModel::load_vars` |
| the layer tables | `UI\ComfyCakes\Layers.xml` and `GlazeLayers.xml`; `ComfyCakesGame::GetGlazeXML` (`0x10005CAA8`) selects glaze by icing colour and pan shape | `CakeModel::load_layers` / `load_glazes` |
| the tool stations | each tool document's `<Station>`: CakePan 1, Batter 0, Decoration 2, Filling 3, FinalTouch 4, Icing 5 | `App::cakes_tool_station` |
| what a button does | `Tool::UseTool` (`0x1000751CC`) → `Cake::CanAddLayer` / `CanRemoveAndAddLayer` / `AddLayer` | `CakeModel::use_tool` |
| the target | `ComfyCakesGame::GenerateCorrectCake` (`0x100060460`) | `CakeModel::generate_correct` |
| judging | `ConveyorBelt::SlideRight`'s harvest (`0x100062D64`) + `ComfyCakesGame::OnCakeDone` (`0x10005C120`) + `CheckEndCondition` (`0x10005C7E8`) | `App::cakes_judge` |
| the score | `ComfyCakesGame::ComputeScore` (`0x10005CC4C`) | `App::cakes_compute_score` |
| the belt | `ConveyorBelt::SetCakePosition` (`0x100063074`), nine slots: 0 the chute, 1 the line's entry, 8 the shipping end | `CakeModel::belt_position` |
| the Chef | `PurbleChef::Show` (`0x10006FD1C`), `OnEnterState` (`0x10006FFEC`), `Init` (`0x10006F7DC`) | [cakes_chef.cpp](src/viewer/cakes_chef.cpp): `cakes_chef_show`, `cakes_chef_advance`, `cakes_draw_chef` |
| the tool machines | each tool document's `<FallingSprite>`, `<ActionSprite>`, `<SplatSprite>`, `<StreamSprite>`, `<BaseSprite>` and the three shape fills, with their `<*Anim>`s | [cakes_tools.cpp](src/viewer/cakes_tools.cpp): `cakes_tools_load`, `cakes_tools_start`, `cakes_tools_advance`, `cakes_tools_draw` |

`--cakes-solve` plays a whole round out through the same click path the mouse uses, the way
`--pairs-solve` does for Purble Pairs. After correcting target generation, measured default-seed
rounds are **Beginner 5 correct, 20 moves, final 1650**; **Intermediate 5 correct, 35 moves,
2250**; **Advanced 6 correct, 66 moves, 3600** — all WON, with zero missed cakes in both art modes.
Section 44 corrects the decoration operation row and final-touch handling; Advanced now has
62 minimum moves, superseding section 35's 57 / final 2600 and section 34's 54 / final 1600.
These are test scores, not original-game scoring parity.

The Chef's states are 0 hidden, 1 idle, 2 entering, 3 leaving, 4 talking, 5 win, 6 lose; his six
movies are `Chef{Idle,Enter,Leave,Talk,Win,Lose}Movie.xml`, and his bubbles are his own
`COMFYCAKESTIPS.XML` (38 tips) in the same White/Red/Green styles the Pairs tips use.  He is
anchored at the room origin, not at the document's `<PurbleChef>` 900,400: his idle movie's first
frame already places his parts at x 643..690, y 401..442.

`GenerateCorrectCake` (`0x100060460`) adds pan/batter for each cake layer and filling **only between
layers**, then toppings. Beginner now has **3 visible layers** (batter, icing, decoration),
Intermediate **5**, and the default Advanced fixture **6** (its toppings vary). The old 4 / 7 / 10
counts were incorrect. The target's height is no longer counted twice.

Press **1 / 2 / 3** in the room to start Beginner / Intermediate / Advanced; **N / R** restarts
at the current difficulty, including after a win or loss. These reconstruction shortcuts cancel
any old machine animation before resetting. Wax paper stays visible under every belt cake, so an
empty working slot is no longer indistinguishable from an empty belt.

#### Each machine works on its own slot, and plays its own animation

`ComfyCakesGame::InitGameUI` hands the six `<Tool>` elements their belt positions as it loads them
(`0x10005DABC`: `Tool+60 = the tool count so far + 2`), in COMFYCAKESGAME.XML's document order —
so **CakePanTool slot 2, BatterTool 3, FillingTool 4, IcingTool 5, DecorationTool 6,
FinalTouchTool 7** initially. `ShowTools` (`0x100060954`) then shifts Beginner's pan/batter
containers **100 px right**, assigns them **slots 3 / 4**, hides filling on Beginner, and hides
final touch except on Advanced. Icing and decoration remain at slots 5 / 6. The pan's machine
Base also has an independent **25 px** X offset, which does not move its buttons. `Tool::UseTool`
(`0x10007525C`) takes `ConveyorBelt::GetCake` at that slot, so a cake has to be slid under its
machine before the machine can work on it: the cake enters at slot 1, is built as it is slid right
through slots 2..7, and is judged when it reaches the last slot (8).  Every slide still moves each
cake exactly one slot.

A click is `Tool::UseTool`:

| Branch | Engine | Reconstruction |
|---|---|---|
| the cake cannot answer it, or the machine has no cake under it | two `GameAudio` protos, then `NodeBase::ResumeAnimation` on the machine Base with `MachineUnusableShakeAnim.xml` (a `+6 / -12 / +6` X-position run, 0.2 s), and the `NeedWaxPaper` bubble when no cake is in reach | `Con_CakeButtons`, `cakes_tools_shake`, `refused, the cake cannot take it` / `there is no cake under it` |
| the cake can answer it | the machine's animation for the button's `<Type>`, then `Tool::FinishAddTool` (`0x10007548A`) | callback-sequenced effects; duration comes from the scheduled action, drop and effect timelines, not a fixed 0.6 s |
| the cake's top layer | batter hides its replaced top and imports the matching shape fill onto the cake | immutable pre-use stack plus one shape fill; other ingredients retain the existing stack until their effect/reveal cue |

The machines' parts and their animations come straight out of the tool documents:

| Tool | Parts | Per-flavour animations |
|---|---|---|
| CakePan | `CakePanSquishSprite` (31,0), `CakePanMachineSprite` (-14,-298) | `CakePanMachineFallingAnim` (6 frames, 0.5 s), `CakePanMachineActionAnim` (8, 0.2 s) |
| Batter | `FallingBatterSprite` (10,-130), `BatterMachineSprite` (-32,-298), and Round/Rectangle/Heart`BatterFillSprite` (-32,0) | the falling stream `FallingBatterAnim` (8 frames over 0.6 s), one action anim per flavour (11 frames, 0.6 s), and one fill anim per shape (7 frames over 0.6 s) |
| Filling | `FillingSplatSprite` (2,-160), `FillingMachineTubesSprite` (2,-298), `FillingMachineSprite` (6,-242) | the splat (4, 0.4 s), one base anim per flavour (2, 0.1 s), the action (6, 0.5 s) |
| Icing | `IcingSplatSprite` (56,0), `IcingStreamSprite` (52,0), `IcingMachineActionSprite` (-4,-308) | the splat (6, 0.5 s), the stream (4, 0.5 s), and six action anims — three single-flavour and three that rotate the base between flavours (`ToolIcing::PlayRotateAnimation` `0x1000783B8`), five of them reversed |
| Decoration | `FallingDecorationsSprite` (35,0), `DecorationMachineSprite` (-6,-298) | the falling decorations (10, 0.5 s) and one action anim per decoration (11, 0.2 s) |
| FinalTouch | `FinalTouchMachineActionSprite` (4,-190), `FinalTouchMachineBaseSprite` (20,-301) | the sugar/burnt action anims (index `type - 13`, so the glaze button plays the sugar strip) and the base rotation (6 frames, 0.5 s) |

A `ResourceAnimation` is a list of `<Animation>` blocks, each with an `<Affects>` property, a
`LinearAbsolute` (`<Start>`→`<End>`) or `LinearRelative` (`<Count>`) run and a `<Time>` in seconds,
with `<WaitForComplete/>` separating sequential groups. [animation.cpp](src/animation.cpp) handles
parallel blocks, independent Position axes, relative accumulation and frame-crossing `IntMessage`
cues. **Correction:** the session-33 claim that UVs read beyond a sprite's `FrameCount` was wrong.
`NodeSprite::UpdateCurrentUVs` (`0x100094484`) clamps UVs to `FrameCount - 1`; logical frames can
still trigger callbacks. The previous override drew neighboring sheet art and made machines
appear corrupted or disappear. The viewer now respects the declared count.

The frame loop updates the Chef, machines, belt and dispatch without rebuilding the static room
per tick. Unchanged HUD text is retained. Scripted `wait:MS` now waits without blocking that loop.

The matrix row is the top layer's **class**, not the tool station; filling shares batter's class 0.
`Cake::AddLayer` (`0x1000567EC`) confirms operation 2 consumes the replaced layer, so batter removes
its pan. The older claim that this was still unsettled is superseded by sections 31 and 34 of the
RE notes. A second batter layer needs a new pan first, recorded in the target's move sequence.

**Final-touch correction (section 44):** decoration row 2 is `0,2,4,3,5,3`, not all zero.
The Advanced last machine now accepts a decorated cake. Glaze replaces its icing in place with
`GlazeLayers.xml` art for the same colour/shape; powdered sugar sits below the top decoration.
Both finishes preserve the decoration and appear on generated TV orders as well as belt cakes.
`tests/cakes_regression.py` now checks **27 viewer runs**, including normal clicks on both
last-machine buttons and Advanced finish art in both DPI modes; CTest still checks 3000 generated
buildable targets plus explicit decoration/final-touch fixtures. Evidence:
`C:\re\traces\cpp\cc\s44`; the separate Cakes UI regression passes **30 runs**.

Idle bodies and bases are visible; falling pans/batter/decorations, splats, streams and batter
fills are hidden until their machine runs. Both idle and animated machines belong to one dynamic
layer, avoiding a second stationary machine behind an animated/shaking copy. Shared effect
animations now select sprite flavour/shape independently. The pan/decoration drop endpoints and
icing effect anchors use their recovered callbacks. Pan/decoration drops now integrate the
recovered integer 60 Hz velocity/acceleration, and effect starts follow authored frame cues.
Some removal/replacement transitions and audio remain approximations. The TV target uses the
shipped **80% scale** and shape-dependent vertical offset (`CakeScreen::SetCake`, `0x100057C78`).

#### Conveyor, wrapping and the next order (session 35)

[cakes_transport.cpp](src/viewer/cakes_transport.cpp) owns the roller, chute and dispatch clocks.
Cake placement uses the fixed `98*p - 66` slot grid. Slides fit the authored **92 px / 0.5 s**
path to the next **98 px** slot, preserving timing without a final snap. Retaining the raw endpoint
(session 35) caused a cumulative **6 px left offset per right slide**; session 38 corrects that
claim and implementation. Chute entry also reaches the same grid. This is a reconstruction
alignment correction, not verified original-game animation parity. Left/right roller frames run
in the matching direction.
Correct cakes remain visible until BoxDrop covers/fades them, then a wrapped CakeBox travels on its
**1.4 s** path and the delivery sign plays. Wrong cakes animate the trash, red light and blinking
readout. Gift boxes are hidden when idle, and pending shipments queue rather than erase a running
gift. Hover/DPI rebuilds retain clocks; restarting cancels them.

A correct cake generates a visibly different next TV order; a wrong one retries its order. Original
`ConveyorBelt::AddCake` (`0x100062E44`) regenerates after success on Beginner or time attack.
Doing so for untimed Intermediate/Advanced too, and avoiding immediate repeats, are requested
usability additions. Move minima now accumulate for the different successful orders.

Checks: CTest includes [animation_test.cpp](tests/animation_test.cpp) and the 3000-target model
fixture. [cakes_regression.py](tests/cakes_regression.py) checks layout, all-difficulty solves,
changing targets, reset/navigation and Pairs (23 runs). [cakes_animation_regression.py](tests/cakes_animation_regression.py)
samples all six machines, roller motion, wrapping/dispatch/trash, reset and hover in both DPI modes
(42 normal-click captures). Arguments: build directory, local sample directory, output directory.
Timing samples are tolerant and are not a claim of original-game pixel/timing parity.
[cakes_belt_alignment_regression.py](tests/cakes_belt_alignment_regression.py) checks fixed slot
coordinates after repeated slides, reversals, moving hover and replacement chute entry across
all three difficulties and both DPIs (36 captures; same directory arguments).

#### Comfy Cakes UI/visual corrections (session 36)

- The TV frame's already-qualified key is not qualified twice. It now draws **before** its purple
  screen and glass, rather than painting opaque black over them; there is still one frame owner.
- Each `LAYERS.XML/Shape` retains its own delta: round/rectangle batter **-23**, heart **-19**.
  Drawing, tool effect endpoints and model replacement/removal use the selected shape, fixing the
  4 px-per-layer gap under icing/decoration (3.2 px on the TV).
- Dispatch platform/right-belt frames use shipping phases, not unrelated main-belt/chute updates.
  The platform still resets at the end of its own animation. Exact platform cue timing is approximate.
- Arrow focus halos are centred on their button rectangles; this placement is a requested visual
  correction, not a newly observed original-game coordinate. Only the focused control glows.
  **Superseded in session 42:** the arrow and tool `*HighlightSprite`s are the tip manager's
  instructional cue, not focus art, and no glow is drawn for the pointer any more.
- [cakes_ui_regression.py](tests/cakes_ui_regression.py) checks 30 local-asset runs in both DPI modes:
  chooser choices, modal win/loss actions, actual post-navigation captures, purple TV pixels,
  stable idle dispatch-platform UVs through slides/hover, and button-art-only arrow focus.
  CTest also covers per-shape heights and replacement. `--cakes-solve` is now one-shot so its
  result-panel New Game starts a playable round rather than solving it again.

#### Tool focus and shipping readout corrections (session 37)

- Tool halos are centred on the combined visible button rectangles, including sprite registration
  and Beginner's 100 px shift. Buttons and hit tests do not move; one group is highlighted at a time.
  Like the arrow correction, this is a requested visual alignment, not original-coordinate parity.
  **Superseded in session 42:** the tool glow is not drawn for the pointer at all, so nothing is
  centred and no group is highlighted on hover.
- The top-right readout now uses **frames** of `ScreenSlotSprite`'s single animation, not animation
  indices: frame 2 stays visible after a correct cake; frame 1 flashes on the next empty slot after
  a rejected cake. Unfilled slots are transparent, removing the two rows of permanent dashes.
  `InitGameUI` (`0x10005D018`) sets initial alpha 0; `OnCakeDone` (`0x10005C120`) selects frame 2/1.
  Rejection uses the shipped four-pulse, 2-second alpha animation and a separate judging clock,
  so a queued gift cannot delay the flash. Hover retains state; restart clears it.
  This corrects session 35's overly broad claim that the readout was already working.
- [cakes_focus_readout_regression.py](tests/cakes_focus_readout_regression.py) checks every visible
  tool group on all difficulties, correct/wrong/queued/expired/reset readouts, and both rows in
  full solves, with both DPI modes (**54 runs PASS**). Its focus check is "the hovered button draws
  its own second `<Animation>` and no glow" since session 42. It uses the same three directory
  arguments as the other tests. Existing UI/gameplay/animation regressions also pass: **149 viewer
  runs total** plus **2 CTests**, evidence under `C:\re\traces\cpp\cc\s37-*`.

#### Comfy Cakes sound effects (session 39)

Comfy Cakes initializes the shared SDL audio cache on direct starts and menu entry, and uses
its shipped `CC_*` manifest keys rather than the nonexistent `Con_Cake*`/`Con_Chef*` names.
The 19 cached events cover the six machines (both final touches), refused tools, belt arrows and
conveyor, correct/wrong judgments, win/loss, new TV orders and Chef entry/exit. Manifest volumes
are preserved; `--mute` covers both games. The TV ding is not played for Chef talking/losing.
Sound cues use the reconstruction's gameplay events; precise original machine callback timing
and manifest buffer limits are not claimed. No new per-tick asset loads or scene rebuilds.
[cakes_audio_regression.py](tests/cakes_audio_regression.py) checks decode and successful SDL
queue/resume, normal clicks, both final touches, solves, loss, mute and Cakes/Pairs navigation
with a real playback device (**11 runs PASS, 19/19 events decoded, WASAPI**). Existing gameplay
checks also pass (**23 runs**, including both DPI modes and three Pairs solves), and **CTest 2/2**.
Evidence is under `C:\re\traces\cpp\cc\s39-audio` and `cc\s39-general`.
It does not establish listening or exact original timing parity.

#### Comfy Cakes hover correction (session 42)

The machine, arrow and TV glow the room was drawing on hover is not hover art: it is the
`CCTipManager`'s **instructional cue**, the blink that points at the control the player should
click next. Each `/HighlightSprite` is created hidden with its animation stopped (`InitGameUI`
`0x10005D449`/`0x10005D5D5`, `Tool::Init` `0x100074EDD`, `CakeScreen::Init` `0x100057A9C`), and
the only code that resumes one is the tip manager, beside the tip that says what to do:
`OnIdleTimerFired` `0x100058438` resumes the pan tool's glow and `CakeScreen`'s
(`CakeScreen::PlayHighlightAnimation` `0x100057C64`) with
`GamePlayIdleStart` ("Start your cake here!") and the slide-right glow with `GamePlayCakePanIdle`
("Use the RIGHT ARROW"); `OnToolButtonClick` `0x10005869C` and `OnArrowButtonClick` `0x1000585FC`
do the same. `Tool::UseTool` stops the glow on the click that uses the tool (`0x100075246`) and
`Tool::HandleEvent` restarts it on every `Event_AnimationComplete` (`0x10007550A`), so the 2 s
four-pulse `*HighlightAnim` loops — no focus art behaves that way. A tool button's and a belt
arrow's own focus art is `<Animation>` 1 of their three (`NodeButton::HandleEvent`'s FocusGot case
`0x1000AAC92`), which `build_cakes`/`cakes_append` still draws. `build_cakes` no longer draws any
`/HighlightSprite`; the instructional cue itself is not modelled, so no glow appears at all.

Measured on the default Beginner room at 1:1: idle and hovered dumps both draw **43 sprites** and
zero `HighlightSprite` lines; the hovered pan-round button's source moves from **799,398** to
**799,435** and the hovered left arrow's from **955,558** to **717,853**, with the neighbouring
buttons unchanged. The hover diff against idle is the button's own rect only — `(186,298)-(239,333)`
for the pan button, `(171,416)-(236,466)` for the arrow — where the shipped glow previously added
`(173,285)-(251,417)`. Regressions after the change, both DPI modes: focus/readout **54 PASS**,
UI **30 PASS**, animation **42 PASS**, belt alignment **36 PASS**, gameplay/layout/Pairs **23 PASS**,
**CTest 3/3**, and `cakes_compare.py` still reports 0 uncovered pixels differing and 0 strays.
Evidence: `C:\re\traces\cpp\hover` (logs and captures `fix-{tool,arrow,idle}.{log,ppm,png}`).

Remaining Comfy Cakes gaps: the tip manager's instructional state machine (its 30 s idle timer, the
`GamePlayIdleStart`/`GamePlayCakePanIdle`/`GamePlayDoubleCakePan`/`GamePlayDoubleBatter` tips and
the glow each one plays), automatic/time-attack timers, simultaneous belt cakes/line-clog loss,
full removal/replacement choreography, exact audio cue timing/buffer limits, decoration
placement/orientation, TV transition behavior and persisted round state.

Regression checks: `ctest --test-dir build/pple-res --output-on-failure` checks 3000 generated
cakes and widget ownership. `tests/cakes_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR` checks all
three difficulties in both art modes, normal clicks, solves, menu roundtrip, keyboard difficulty
changes/restarts and the three Pairs solves (23 runs). `--script-click` accepts `key:1`, `key:2`,
`key:3`, `key:n` and `key:r` to exercise the real SDL keyboard event path. Captures and logs for
this correction are under `C:\re\traces\cpp\cc\s34`.

### The Professor, his tips, and the two dialogs drawn inside the game

The shipped screen is not just a board. The Professor animates through a six-state machine and
speaks through `TIPS.XML` bubbles, and the two things it opens as **separate Win32 windows** —
the difficulty chooser and the end-of-round score dialog — are drawn inside the 800x496 scene
here, because a web or mobile build has no second window:

| Piece | Where it comes from | What the viewer does |
|---|---|---|
| Professor state machine | `Professor::Update` `0x10008C088`: state 1 hidden, 2 entering, 3 exiting, 4 idle, 5 talking, 6 win, 7 lose; `MoveIn` `0x10008C354`, `MoveOut` `0x10008C3B0`, `DisplayTip` `0x10008C290`, `DisplayIntro` `0x10008C3F0` | all six movies are loaded once and the same states are replayed, on the 30 Hz tick |
| Tips | `UI\Concentration\TIPS.XML` (28 tips) parsed as `TipManager::parseXml` `0x1000930B8`, drawn from `Tips\Styles\Green\TipStyle.xml` as `Tip::Initialize` `0x1000A5834` and `Tip::Arrange` `0x1000A49A0` | a 9-slice bubble at `615,150` (intro/win/lose), `615,125` (between boards) or `615,120` (the second intro bubble), sized from the title and the wrapped body, with the shipped `Segoe UI` 12/8 pt colours |
| Start of a game | `DisplayIntro` shows `ProIntro`, then `ProIntroNext`, then `Pro_Idle`; a later game gets the single `ProWelcome` bubble instead | `--menu`/`--difficulty` starts play `Pro_Enter` (11 frames at 12 fps) → `ProIntro` → `ProIntroNext` → idle; an explicit `--pairs` start skips it, and `--intro`/`--no-intro` choose |
| Between boards | `Jar::StopExplosion` `0x100083A20` sets `ProIntLevelChange` (Intermediate) or `ProAdvLevelChange` (Advanced) at `+0x48 = 125` and calls `MoveIn` before `Board::NewLevel` | the same tip, re-shown after the level's art is rebuilt |
| End of a round | `OnEnterWonGameState` `0x1000821D0` (a random `ProWin1..3`), `OnEnterLoseGameState` `0x10008259C` (a random `ProLose1..2`), then `OnWinAnimStop` `0x100082670` / `OnLoseAnimStop` `0x10008272C`, which is where the shipped build opens `DialogBoxMCE` template 221 | `Pro_Win`/`Pro_Lose` + the tip, then `Pro_Exit`, then an in-canvas panel with `Game Points: N` (MUI 59071), `Final Score: N` (57409), the `Rank:` title from `RANKS.XML` and three buttons — `New Game` (42695), `Change Difficulty`, `Main Menu` (56503) |
| Difficulty chooser | the dialog whose strings are MUI 213 `Select Difficulty`, 214 the question, 215..217 the three options with their grid descriptions and 219 the Options note | the classroom background, the waiting Professor and a green panel with those three buttons; hovering uses the style's title colour and a bold pass, because the `NewGameButton` art this build ships is transparent (`NewGame`/`ExitGame` measure 0% art — the Media Center host paints them) |

The style's `Tip::Arrange` geometry is what the panels and their buttons are built from, so both
dialogs follow it: the back starts at (`LeftBorder.w`, `TopBorder.h`) and is sized
`width - left - right` by `height - top - bottom`, flush against the borders, and the four borders
are stretched between fixed-size corner sprites. The style's top and bottom borders are 13 and
14 px, so 27 px of a 60 px button is frame and the name and its description are centred in the
rest — at the Media Center button art's 51 px the two lines ran over the bottom border.

The shared panel loader uses `sprite_frame_rect` for every piece (including close/check buttons),
so `<PreppedForFilter>` frames start one pixel inside their atlas slots. The previous raw slot
origins included the filter gutters and produced dark stripes/broken joins on dialog corners and
buttons. This fixes the white/red/green styles in all three games, including the difficulty and
result panels, without changing layout, hit boxes, scaling or texture ownership.

Two engine details the bubbles follow: a tip with `<NoTimeout>` waits for a click and the
`<CaptureGlobalClick>` default is what makes the first click anywhere advance it, while a tip
with a `<Duration>` expires on its own and does not swallow clicks. `--no-tips` draws no bubbles;
the Professor still animates and the win/lose panel still appears.

A tip's panel comes from its `<Type>`, and there are **three** style documents, not one:
`TipManager::ShowTipIfEnabledAtLocV` (`0x100092AC8`, `0x100092C06`) indexes them with the type
`TipManager::parseXml` (`0x1000930B8`) assigns — `Intro` 0 → `Tips\Styles\White`, `Error` 1 →
`Tips\Styles\Red`, anything else 2 → `Tips\Styles\Green` (the three `Str` globals at
`0x1000FDBF0`, `0x1000FDC08` and `0x1000FDC20`). So the Professor's `ProIntro`/`ProWelcome` and
the between-boards bubbles are white-framed, the one `<Type>Error</Type>` tip (`BlankCard`) is
red-framed, and the flip and sneak bubbles are green. Each document names its own nine pieces, so
the viewer reads the sprite paths out of the XML per style rather than rebuilding them from a
prefix.

Two of those tips are the engine's *simple* tips (`CONTipManager::ShowSimpleTip` `0x100085C8C`),
shown straight from an object rather than through the Professor: `SneakPeak` over a sneak token
and `BlankCard` over a matched cell, both at `615,125`. `SneakPeakArea::HandleEvent`
(`0x100084D3C`) raises the first on **MouseEnter** and `Card::HandleEvent` (`0x10007C1F4`) the
second on **MouseDown**; the reconstruction draws both on **hover**, deliberately, because that is
where the player looks for them here and because a matched cell otherwise swallows the click it
would need. They are state rather than events — a card that becomes matched under a pointer that
has not moved still raises `BlankCard` — so `update_hover_tip` runs from the 30 Hz tick as well,
and an expired bubble is latched until the pointer moves to another object.

`--script-click` entries may be `wait:MS`, which is how the intro chain is checked without a
mouse: `--menu --script-click "100,180;400,175;wait:1500;700,200;wait:600;700,210"` picks
Beginner, waits for `Pro_Enter`, then clicks the two bubbles. `move:x,y` is the same injection as
a mouse move, so the hover bubbles are scriptable too:
`--pairs "Beginner,0,0" --no-deal --script-click "move:25,470"` shows the `SneakPeak` bubble over
the first token.

`--sprite` shows one frame of a sprite node definition; `--state N` picks which `<Animation>` of
a button node, because a button carries one per visual state (0 is what an untouched button
draws, 1 is `<enter>`, 2 is `<down>`). `--scene` is the layout interpreter: it reads a UI
document, walks the named instances, resolves each `<Desc>` to its node definition, and draws
every sprite frame and label in document order. `--save` writes one decoded resource 1:1 as PPM
for offline analysis.

Two engine rules that the layout interpreter applies because the shipped data depends on them:

* **The HUD containers are re-parented by code, not by the document.**
  `CONCENTRATIONGAME.XML` lists `TimeBarContainer`, `TimeBarBkg`, `SneakPeekContainer`,
  `JarContainer`, `ProgressMeterContainer` and `ProfessorContainer` as root-level siblings, but
  each object's `InitUI` looks its own elements up by path and creates them under its own
  container — `TimeBar::InitUI` `0x10008517C`, `SneakPeakArea::InitUI` `0x1000849C0`,
  `Jar::InitUI` `0x100083218`, `ProgressMeter::InitUI` `0x100084720`,
  `Professor::InitUI` `0x10008BD94`. `apply_container_offsets` applies that as a post-pass, which
  is why `TimeBarBkg` now draws at `60-17+3,21` = `63,4` rather than at `3,21`.
* **A node sits at its own (x, y) and a `<scale>` scales only that node's own size.**
  `NodeBase::UpdateZero` `0x10008CB8C` gives a child `parent_world + child_local` and does not
  inherit the parent's scale; `NodeSprite::UpdateZero` `0x100093740` then *subtracts*
  `scale * RegistrationX / 100` from the world rect, so the drawn corner is
  `(x, y) - reg * scale / 100`. That last sign is what makes the four menu hot-spots line up with
  the icons painted in `PurblePlaceForeground.png`.

`--script-click "x,y;x,y"` injects clicks in **scene** coordinates through the same handler the
mouse uses, so menu → game → menu and the card flip can be verified without a mouse, and a
`move:x,y` entry is the same injection as a mouse **move**, so a hover is reachable without one
too (the loop routes motion on every screen, and `SDL_EVENT_WINDOW_MOUSE_LEAVE` clears it);
`--shot-delay MS` waits that long before capturing, which is what catches a flip mid-animation.
`--pairs-solve` plays the whole round out — every `<Level>` of the difficulty — by clicking
matchable pairs (equal `<id>`, or the Wild Card against anything) through the same handler the
mouse uses, and `--pairs-ticks N` advances the engine's fixed 30 Hz step N times first, so the
time bar, the red threshold and the loss state are reachable immediately. `--bench N` renders N
frames with vsync off and reports the card-layer cost; `--bench-build N` reports the cost of N
whole scene rebuilds.

Both hooks — and `--pairs-reveal`, `--pairs-ticks` and `--no-deal` — skip the **deal-in** for the
same reason they skip the jar's flight: a scripted run has no frames to watch it with. A normal
`--pairs` start deals: every card grows from 10 percent of its size to full over 45 ticks after
its own `rand() % 60` ticks of delay, the board ignores clicks until the last card has arrived,
and the first click on a card stops the whole thing and brings every card in at once
(`Card::DoCardClick` `0x10007CD65` → `Board::StopIntroAnimation` `0x10008B45C`). The screen's own
buttons are not gated by it, so `ReturnToMainMenu` works during the deal. The Professor's intro
bubbles start when the deal ends, which is where `Board::Update` calls `DisplayIntro`.

`--loading [MS]` starts on the game's own loading screen — `Engine_SetBackMode`'s
`UI\PurblePlace\Loading.xml`, one 800x496 sprite on `Loading.jpg`, with the two `LOADING...`
labels `PurblePlaceManager::Init` hides once the load finishes — and after MS milliseconds (1200
by default) hands over to the menu when `--menu` is also given. Without `--menu` it stays there,
so `--shot` can capture it. MS stands in for however long `Init` takes in the shipped game, which
the reconstruction cannot measure.

`--pairs-save PATH` runs `OnEnterWonGameState`'s statistics update at the end of a won round —
one more game played and won, the final `CalcEndGame` total into that difficulty's high-score
table, `TilesMatched` and `TilesTurned` accumulated — and writes `PATH` through `pple::rgmh_write`.
It is opt-in, so a viewer run never touches the player's own file. A path that does not exist
starts from a default with the shipped tip list; the game also stores a thumbnail of the finished
board, which this does not.

Sheet handling follows the engine: a `.sht` reference is loaded as `<stem>.jpg` (colour) plus
`<stem>.bmp` → `<stem>.BMP.CAB` (alpha) and the two are composed, a frame whose node sets
`<PreppedForFilter>` sits one pixel inside its slot with a two-pixel stride
(`NodeSprite::UpdateCurrentUVs`, `0x100094552`), and every surface is padded to the next power of
two before it becomes a texture, because that is the size the node rectangles are expressed in.

### The double-DPI (x2) art

The game ships every image twice, and `g_bDoubleDPI` is what chooses between the two sets
(`ResourceTextureD3D::SetNameForDPI` `0x1000A80DC`, `Recreate` `0x1000A7FEC`, and the split
branch above). `--hidpi` draws that set instead of the 1x one, `--no-hidpi` forces 1x, and with
neither flag the viewer picks by window scale: **x2 from 1.5x upwards**, where upscaling the 1x
art would soften it. The default 1400x900 window is 1.688x, so it now uses the x2 art; a 1:1
800x576 capture stays on the 1x art, which is what keeps the recorded captures comparable.

Nothing about the layout changes. The x2 resource is exactly twice the padded 1x texture
(`pot(2w) = 2*pot(w)`), so a source rectangle authored in 1x space is read at twice its offset
and size and every destination rectangle, registration, hit test and label stays where it was.
The frame loop prints which mode it settled on, and a teardown line counts how many sheets came
from x2 resources:

```
hidpi: double-DPI (x2) art, window scale 1.688, threshold 1.50
hidpi: 9 sheet(s) drawn from the x2 resources, 0 fallback(s) to 1x
```

A resource with no x2 sibling is named on its own `hidpi: no x2 resource for …` line and drawn
from the 1x art; on the menu, the loading screen, the difficulty chooser and all three Pairs
boards there are none — the eleven sheets the menu and Pairs use all have an x2 form, five of
them as four split pages and six as an `…x2.jpg`/`.bmp.cab` pair.

Resizing the window across the threshold switches modes mid-run: the viewer releases the draw
lists, drops the sheet cache and rebuilds the current screen (see the
[viewer module map](src/viewer/README.md)). While a tip or the end-of-round panel is up — both
borrow sheet textures — the switch waits for it to close.

### Resuming a round

`--pairs-state PATH` is the resumable round. Leaving through `ReturnToMainMenu` writes it the way
`ConcentrationGame::SaveGameState` (`0x100082B40`) does — an `RGMH` container holding a board
thumbnail and a UTF-16LE XML payload — and when `PATH` already holds one, the start resumes it:
with `--pairs` it goes straight to the board, and with `--menu` the menu reports
`Saved game (<difficulty>, score N, M match(es)): press ENTER to resume`. Section 27.6 of
`D:\Development\re\notes\purbleplace.md` has the format and its limits.

The thumbnail is a 256x192 PNG written by `pple::png_write` (`src/png_write.cpp`): 8-bit RGBA in a
single *stored* DEFLATE block, which every decoder accepts and which is why the file is ~208 KB
rather than a few kilobytes. It is a picture of the board — each cell's face up, down or cleared,
a colour keyed off the card id, and the golden bonus indicator — and is decoded back with the core
`png_decode` before it is written, so a corrupt block cannot be saved silently.

`--movie` renders a `<Movie>` skeletal animation: `<Library frameCount=>` names the length,
each `<ImageInstance name=>` is a part resolved through `<ImageDef xml=>` to an ordinary sprite
node definition, and each `<Frame num= x= y= rotation= xscale= yscale= alpha=>` is a keyframe.
A keyframe applies only on its own frame number and sets only the attributes it carries, so an
omitted attribute holds its previous value and every property is a **step** function — the value
of the last key at or before the frame being drawn. There is no interpolation
(`PPMovieInstance::UpdateFrame`, `0x10008761C`); `<Frame num="1"/>` means "no change". Interpolating
instead blends each part across the whole gap between two keys, which puts several *different*
images partly opaque on top of each other — the Professor's open eye mixed with the solid blue
closed lid, so it loses its pupil and the room tints its soft rim, and the exit's one-frame blur
flash spread into a slow glow. `--anchor X,Y` places the movie's own
coordinate space. In `--scene` mode every movie instance is resolved and reported, positioned
ones are drawn, and `--movie-name NAME --anchor X,Y` draws one that the document does not place
(the Concentration screen's nine `Professor*`/`SneakMovie`/`FireWorks` slots are all of that
kind). Note that `--movie-name` always draws **frame 0**; use `--movie XML --frame N` to look at
another frame.

For every sprite the scene renderer also reports **art coverage** — what fraction of the
declared sheet rectangle is non-transparent (and, for sheets that have no alpha channel,
non-black). A rectangle can legitimately be fully transparent: un-hovered `SHEET18` buttons draw
animation 0, which is empty because the menu icons are painted in
`UI\PURBLEPLACE\PURBLEPLACEFOREGROUND.PNG` and each button is an invisible hot-spot over its
icon. See section 10 of `D:\Development\re\notes\purbleplace.md`.

Keys: `LEFT`/`RIGHT` or `PGUP`/`PGDN` navigate, `SPACE` toggles integer scaling (`off` by
default, so a scene fills the window; `on` keeps whole-pixel scaling), `S` writes the current
image as PPM, `N`/`R` start a new Purble Pairs round, `ESC` returns from the game to the menu and
quits from the menu. Items decode lazily; sheet textures and label fonts are cached. `--shot`
renders one frame into a hidden window and reads the back buffer; the read must happen **before**
`SDL_RenderPresent`, which invalidates it.

## Components

* `inflate.cpp` — raw DEFLATE (RFC 1951), stored/fixed/dynamic blocks, canonical Huffman.
  Back-references resolve against the caller's buffer, which makes MSZIP history carry-over
  automatic across CFDATA segments.
* `cab.cpp` — `MSCF`, `CFFOLDER`, `CFFILE`, `CFDATA`, the `"CK"` marker; verifies every block
  and the final size.
* `pe_image.cpp` — DOS/NT headers, sections, RVA→offset, the three-level resource directory
  (named UTF-16 and integer entries), CodeView PDB name.
* `png.cpp` — PNG on top of `inflate`: non-interlaced, depths 1/2/4/8/16, colour types
  0/2/3/4/6, `PLTE`, `tRNS` palette and colour-key, all five filters, RGBA8 out.
* `png_write.cpp` — the encoder's counterpart, used by the savegame's thumbnail: 8-bit RGBA in a
  single *stored* DEFLATE block (no compression, so no compressor), CRC-32 per chunk and an
  Adler-32 trailer.
* `xml.cpp` — small XML reader (declaration, comments, nested elements, attributes, entities,
  CDATA), BOM aware for UTF-8 and UTF-16.
* `scene.cpp` — layout loader: instance walk with container offset accumulation, node
  definition resolution, the container positions, label extraction from localisation strings,
  and the frame rectangle including the `<PreppedForFilter>` gutter. `apply_container_offsets`
  reproduces the engine's own HUD re-parenting (see `pple-view` above).
* `concentration.cpp` — Purble Pairs: `CARDS.XML` pools, `<Constants>`/`<DifficultyModes>`,
  the `<CardPairs>` deck, the board geometry and the click/match/tick rules. No SDL.
* `split_list.cpp` — `SPLITLIST.XML`: the per-sheet virtual-size table and the `<stem>x2<page>`
  page resource naming.
* `rgmh.cpp` — the `RGMH` save-file container: read, rewrite, and a first-difference helper.
  The header block is rebuilt from the fields, so a matching round trip proves nothing else is
  stored in it.
* `movie.cpp` — `<Movie>` skeletal animations: `<ImageDef>` part resolution and per-part
  keyframe tracks applied as steps (a key counts only on its own frame number).
* `wma.cpp` — WMA decoding through Media Foundation: a read-only in-memory `IStream`,
  `IMFSourceReader` to 16-bit PCM, and a RIFF/WAVE writer. SDL3_mixer has no WMA decoder, so
  this is the decoder.
* `mui.cpp` — `.mui` `RT_STRING` tables, the UTF-16/UTF-8 helpers, and the localiser hook that
  `localized_default` consults before falling back to the English text in the data.
* `sha256.cpp` — digest helper.
* `view_main.cpp` — 8-line viewer entry point. The former 4,026-line implementation is
  split into 22 responsibility-focused C++ translation units under `src/viewer`;
  the largest is 388 lines. Start with the [viewer module map](src/viewer/README.md),
  then read only the module for the feature being changed. `App` owns session state;
  the board rules remain in the SDL-free core library. `tips.cpp` owns the `TIPS.XML`
  bubbles and their three styles, `professor.cpp` the Professor's state machine,
  `overlays.cpp` the two screens that replace a shipped dialog (the difficulty chooser
  and the end-of-round panel), and `pairs_state.cpp` the resumable round.

## Verification

| Check | Result |
|---|---|
| SHA-256 self-test vs recorded hashes | `PurblePlace.exe`, `slc.dll` match |
| `verify` on the three asset DLLs | 85 CABs, 0 block mismatches, inflated totals equal summed `cbFile` |
| `pngscan` | **114 PNGs decoded, 0 failures** |
| `pple-xml` | **840 XML resources parsed, 0 failures**; 3425 distinct element paths |
| Re-extracted assets vs independent Python extraction | byte-identical |
| `pple-view --shot` | PNG, CAB→BMP and JPEG paths all render; sprite frames verified |
| `pple-view --scene` | `CONCENTRATIONGAME.XML` on its own renders **14 sprites and no labels**, because `ConcentrationGame::LoadGameUI` deletes the five Media-Center nodes; with `--mce` it is **16 sprites + 3 labels**, and `NewGame`/`ExitGame` measure 0% art (correctly transparent — see the note above). Adding `--pairs` hides the three time-bar nodes, two of them templates (`/TimeBarGreen` and `/TimeBarRed` are deleted by `TimeBar::InitUI` `0x10008517C`) and the background on an untimed difficulty, so a Beginner board now reports **6 static sprites** after also hiding the unused sneak animation template and initial sparks (superseding the earlier 8-sprite count), with token sprites drawn separately according to the count |
| `pple-res split` | 11 split sheets, each with exactly four `x2` page resources (44 in `PurblePlace.dll`) |
| `pple-view --sprite --state` | `BUILDAPURBLEBUTTON.XML` state 0 transparent, state 1 light art, state 2 dark art, matching the running game's own menu screenshot |
| `pple-res rgmhrt` | All four `RGMH` files on disk (3 settings + 1 savegame, 83,826–87,542 bytes) re-emitted **byte-identical**; `windowprefs.xml` correctly rejected as not `RGMH` |
| `pple-view --movie` | `PRO_IDLE.XML` 61 frames / 31 parts, all drawn, frames 0/20/35/50 differ; `SMILEYBURSTMOVIE.XML` 88 frames / **322 parts**, all drawn |
| `pple-res pairs --autoplay` | The whole round, not one board: Beginner **1 board, 12 pairs + the odd board's forced Wild Card, score 390**, Intermediate **2 boards, 36 pairs, score 1120**, Advanced **4 boards, 128 pairs, score 3920**, all `WON`, no mismatches. `Board::NewLevel` (`0x10008AAD4`) advances through the difficulty's `<Levels>` list, so Intermediate plays 2 boards and Advanced 4, each with its own `<CardPairs>` and `<TotalTime>`. Beginner was **410** until `Board::ShuffleCards` (`0x100089604`) was reproduced properly: it appends the odd board's Wild Card to the list *before* drawing from it, so the Wild Card is shuffled in like any other card rather than pinned to the last cell, and the autoplay now reaches it while pairs are still left and matches its twin |
| `pple-view --pairs` | Advanced 8x8 draws **all 64 cells** from real card sprites; cell 49x49 at scale 40%, face-up 64 / backs 0 / sprite failures 0 |
| `pple-res muicheck` | Against `PurblePlace.dll` alone: **798 ids referenced, 796 (99.7%) byte-identical** to `en-US\PurblePlace.exe.mui`, 2 divergent, 0 absent. Adding `CardGames.dll` raises the total to 1,154 ids with 245 absent — all of them Solitaire ids that this MUI does not carry |
| `pple-view --mui` | Installs the MUI as the label localiser; the menu title's font changes `Comic Sans MS` → `Segoe UI` and `LOADING...` gains the MUI's trailing space |
| `--scene` label alignment | `<Justify>` decoded as a `DrawText` mask; the menu title now sits vertically centred in its box |
| `pple-res audio` | 150 manifest events (PurblePlace.dll 123, CardGames.dll 27), 147 resolve to a WMA resource, 3 unshipped |
| `pple-res asf` | **All 201 ASF/WMA resources parse**, every one WMA v2, 2ch 44.1 kHz 16-bit, with real durations |
| `pple-res wma --all` | All 201 decode through Media Foundation, 0 failures, 343.35 s total; decoded length agrees with the container's declared duration for **201 of 201** |
| `pple-view --play` | `UI_BUTTON` resolves through `AUDIO.XML` to `PURBLE_BUTTON_STEREO.WMA`, decodes to 0.325 s and plays through the SDL **wasapi** driver |
| `pple-view --pairs` sounds | 14 `Con_*` events decoded once through Media Foundation at the manifest's own volume and played from `ClickResult`, which carries one flag per branch of `Card::DoMatcher`: `Con_TileFlip` on every flip, then `Con_Correct` / `Con_TileFlip_Incorrect` / `Con_PowerUp` / `Con_PurbleChef` / `Con_Mixer` / `Con_TimeDelay` / `Con_SneakPeek_Get`, plus `Con_NewScreen` per board, `Con_BoardCleared` on the clear, and `Con_Win` / `Con_GameOver` at the round's end. The proto→name mapping is `AUDIO.XML`'s **element** order in `<Sounds>` (`GameAudio::LoadGameAudio` `0x10009DDF8`) and was checked against every call site; see section 18.1 of the notes. Note `OnWinAnimStop`/`OnLoseAnimStop` play nothing — `Con_BoardCleared` comes from `Jar::Update` |
| `pple-view --pairs` progress meter | Drawn from its real sprites: `Board::Init` (`0x1000888C4`) copies `UI\Concentration\CardBacks.xml`'s first four `/progpath` values to `(Board+0x20)[i]+0x508`, and `ProgressMeter::InitUI` (`0x100084720`) makes one indicator per `<Level>` at `/ProgressMeterContainer` `665,40`, stacked 55 px apart. The frame is the state: `node+0x180 = 1` while `i < Round+0x124`, which `ConRoundData::ToXml` (`0x10008035C`) names `Level`. Beginner 1 indicator, Intermediate 2, Advanced 4; at Advanced's level 4/4 the first three are filled and the fourth hollow (`s4-meter-zoom.png`), at Intermediate's level 2/2 the first is filled (`s4-levelup-meter.png`) |
| `pple-view --pairs` special cards | All four now act, and each prints its effect: the Chef's cake sweeps every card with the Chef id and plays `Con_PurbleChef`; the Mixer plays `Con_Mixer` and `Board::DoMixer` (`0x100089870`) reshuffles the cards left; the Time Bonus adds `<TimeBonusPercent> / 200 * Level+0x0C` ticks (**+787** on Intermediate's 6300, **+675** on Advanced's 5400) and plays `Con_TimeDelay`; a card carrying the sneak icon pays `<SneakMatchBonus>` 20 and plays `Con_SneakPeek_Get`. A match with **exactly one** Wild Card is the engine's else branch: a flat **+10** with no `MatchMade`, and `Con_PowerUp` — and that branch does not end there. `0x10007D762` takes the *non*-Wild card's id and index and calls `Card::FindOtherMatch` (`0x10007E3F8`), then marks the other card carrying that id `Card+0x45 = 1` with `Card+0xF8 = this`; `Board::SparkAnimate` (`0x10008B4B0`) flips it forward 15 ticks later and `Card::OnReveal`'s `Card+0xF8 != 0 || Card+0x45 != 0` test runs `Card::DoForceMatch` on it (`Card::DoCardClick` blocks the player's own click with `Card+0x70 = 1` in the meantime). Without that twin the card the Wild Card stood in for is stranded and `Board::Update`'s completion test can never be met again. `--wild-early` + `--autoplay` is the regression: **30/30** difficulty-and-seed combinations now finish after the Wild Card is spent on the first partner, and a stranded round used to report `unfinished` |
| `pple-view --pairs` sneak movie | `/SneakMovie` is **26 frames at 15 fps, 23 parts**, reset/hidden until a token click and hidden again at the end. Correction to the session-6 handoff: `0x100084B20` parents its Base to the input Board Base, NOT `/SneakPeekContainer`; `0x100084B35` sets **-18,-70**, not -18,328. `Board::InitUI` passes Board+0x70 at `0x1000892DB`. The movie is cached across board rebuilds. `s7-peek-1to1-350.png` shows the coin/starburst over the revealed board; `s7-peek-1to1-2300.png` shows backs again and one token left |
| `pple-view --pairs` viewport stability | Keep the **800x496 design bounds** (plus the existing 51-pixel MCE hypothesis), not the animated parts' changing extents. Offscreen movie parts are clipped, so peek/burst playback no longer zooms out the entire board. The diagnostic HUD height depends on its text/font: this machine's Pairs HUD is **80 px**, so exact 1:1 captures use `--width 800 --height 576` and crop `(0,80,800,576)`, not the handoff's 556/60 assumption |
| `pple-view --pairs` scoring | `Board::MatchMade` (`0x10008B0AC`) is reached with the multiplier input `(a[+0x50] + b[+0x50]) / 2`, and `Card::Init` (`0x10007B6E4`, at `0x10007B717`) is the only writer of `Card+0x50` and stores 0 — so the shipped build always takes the `*= 3` branch. Beginner's 12 pairs score 360 plus one `<SneakMatchBonus>` of 20 and the odd board's forced Wild Card 30 = **410**; the previous `x2` branch was 2/3 of that. That is the `--pairs-solve` figure; the CLI autoplay now spends the Wild Card on a partner instead and scores **390** (see the autoplay row) |
| `pple-view --pairs` sneak peek (corrected session 6) | Token entries have their own x/y/scale, read by `LoadSneakPeekToken` (`0x10008C557`/`0x10008C559`/`0x10008C58A`): drawn rects **3,448 / 47,448 / 93,448**, each 45x46 at the shipped default 100%. Only `i + 1 <= Round.Tokens` are drawn and clickable. `Card::OnReveal` (`0x10007FA50`) arms **45 raw `/SneakRevealDuration` ticks**, after the model's 10-tick flip, not the 600-tick icon lifetime. Spending costs 5% of the level clock: **0 / 315 / 270 ticks**, truncating float32 and clamping to zero (`0x100084F69`). `pairs --sneak-check` verifies duration, repeated-use rejection, expiry and zero-clamp/loss on a one-token fixture (including Advanced, which normally starts with zero) |
| `pple-view --pairs` card back | `Card::InitUI` (`0x10007B8D8`) calls `Card::InitCardButton(this, Level+0x148)`, and `Card::InitCardButton` (`0x10007CB4C`) builds `Card+0x98` from `GetXml((Board+0x20)[level] + 8)` — the CardBack's `/buttonpath`. So the back is per level: `ShamButton.xml`, `HeartButton.xml`, `SmileyButton.xml`, `GumButton.xml`. `blankcardbutton.xml` is a different node (`Card+0xB0`, "BlankCard") for the intro state. `s4-cardback-compare.png` puts the shipped thumbnail next to the fixed screen |
| `pple-view --pairs` jar | The jar's match count is real and comes from the board. `Jar::DoFiller` (`0x100083B10`) is `want = points / Board+0x28 * Round+0x12C` over that level's `Shamrock/Heart/Smiley/GumDrop_Points.xml` (28 points each), one coin per point built from the level's `/imagepath` with **scale forced to 38** — which is what turns the 111x109 `/imagepath` sprite into a 42x41 coin that 28 of can fit in the 112x112 jar. The run prints `jar: UI\Concentration\GumDrop_Points.xml, 28 coin slot(s), coin UI\Concentration\Gumdrop.xml at 38%`, and `s5-jar-strip.png` shows the jar empty, half full of shamrocks, carrying its hearts into Intermediate level 2, and full of gumdrops at the end of an Advanced round |
| `pple-view --menu` | runs `UI\PurblePlace\PurblePlaceManager.xml` from `PurblePlace.dll`: 8 items drawn, **4 hot-spots** at `509,145 292x250` (Build-a-Purble), `210,153 248x256` (Comfy Cakes), `2,82 212x213` (Purble Pairs), `2,373 93x100` (Exit). `menu-hotspots2.png` overlays the same rectangles on `PurblePlaceForeground.png` and every one lands on its painted icon |
| `pple-view --menu --script-click "330,280"` | enters the Comfy Cakes hot-spot: the menu rebuilds with that button on `<Animation>` 1 and the house gains its bright hover glow in the right place, the HUD names `ComfyCakesButton`, and the click reports the game as unimplemented |
| `pple-view --menu --script-click` | `100,180;400,175;760,470` logs `menu: ConcentrationButton -> Purble Pairs difficulty chooser`, `difficulty: Beginner chosen`, `pairs: ReturnToMainMenu -> main menu` — menu → chooser → board → menu, in one run; the window stays up. The pre-session-10 sequence (`100,180;200,60;760,470`) entered the game directly, because there was no chooser |
| `pple-view --pairs-solve` | a whole round through the viewer's own click handler on all three difficulties: Beginner **12 pairs + the forced Wild Card, score 410**, Intermediate **2 boards / 36 pairs, score 1180**, Advanced **4 boards / 128 pairs, score 4040**, all `WON`, each then swapping to `ProfessorWinAnimation`. The solver matches exact ids first and spends the Wild Cards last, because a single-Wild match is the engine's flat `+10` branch and consumes the partner. A level change prints `board relaid out -> level N/M` |
| `pple-view --pairs` end of round | `ConRoundData::CalcEndGame` (`0x100080718`), run by `OnEnterWonGameState` right after `Con_Win`: `points + time bonus + flip bonus = total -> rank N, "title"`. Beginner **410 + 0 + 2300 = 2710**, Intermediate **1180 + 3000 + 3000 = 7180**, Advanced **4040 + 5000 + 5000 = 14040**, all rank 5, `Rank: Purble Master`. `RANKS.XML` is in `PurblePlace2.dll` |
| `pple-view --pairs` the jar's flying coins | `Board::DoFlyIntoJar` (`0x10008AF54`) spawns one `ArcBehavior` per matched card; `ArcBehavior::Update` (`0x100084128`) walks a quadratic Bezier over `/ArcAnimation/Frames` (25) from the card to `(LidOffSet + CurJar.w/2, CurJar.h/2)`. The apex question is settled: the control point is a constant 375 px above the container, **off the top of the scene**, but a quadratic Bezier only reaches about half way to its control point, so the drawn curve stays in frame. `s6-flight.png` |
| `pple-view --pairs` jar draw order (session 6) | Cached cards and their sparks now precede JarBase/JarBkg; burst, arc coins and tray follow, then JarFg. `Jar::InitUI` (`0x100083218`) creates those jar children in that order. `--script-click "180,30;350,30" --shot-delay 900` visibly crosses over the board; `C:\re\traces\cpp\s7-flight-900.png` and `s7-jar-side-by-side.png`. Static scene draws are retained separately so repeated refreshes do not overwrite them. Warm card layer: **0.0838 ms/call**, 34 calls, first 9.258 ms; no per-tick scene rebuild |
| `pple-view --pairs` match animation | `Card::DoForceMatch` (`0x10007D114`) arms a 10-tick countdown and `Card::InitExplosion` (`0x10007E518`) gives each of the four sparks a source at a corner of the cell and a destination 30 px outside it; `Card::AnimateExplosion` (`0x10007E6D0`) walks them for 30 ticks with `position = src + (dst-src)*(3t^2(1-t)+t^4)` and `scale = 300t(1-t)^2 + 100(1-t)^3 + 600t^2(1-t) + 300t^4` percent. `Card::OnHide` (`0x10007F538`) then hides the art and shows the level's **BlankCard** (`blankcardbutton.xml`), and the coin leaves. `s6-match-strip.png`, `s6-matchnet800.png` |
| `pple-view --pairs` the card deal-in | `Card::InitUI` (`0x10007C0B6`) puts every card in state 6 with `Card+0x14 = 0` and clears `Board+0x1E8`; `Board::InitLevel` (`0x1000895C7`) gives each one `Card+0x40 = rand() % 60` ticks of delay; `Card::DoIntroMove` (`0x10007E104`) then shows its "IntroImage" — a second node built from this level's CardBack `/buttonpath`, i.e. the card back — and grows it for 45 ticks as `Card+0x30 * (6v(1-v)^2 + (Card+0x30/10)(1-v)^3 + 3*(Card+0x30/2)*v^2(1-v) + Card+0x30*v^3)`, `v = Card+0x14 / 45`: 10 percent at `v = 0`, a 7.5 percent overshoot at the half way point. `Card::StopIntroAnimation` (`0x10007E390`) hides the IntroImage and shows the real card, and `Board::Update` (`0x10008A576`) sets `Board+0x1E8 = 1` and calls `DisplayIntro`. The run logs `deal: 25 card(s), delay 0..52 tick(s), 45 ticks each` then `deal: done` after 97 ticks (3.2 s); a click on a card logs `deal: stopped by a click`. Captures `s11-deal-300/900/1400/2000/3000.png` across the deal, `s11-intro.png` after it |
| `pple-view --pairs` jar burst | `Jar::Update` (`0x10008373C`) plays `Con_BoardCleared` when the last coin lands, shows the level's `/moviepath` (`Jar::LoadMovies` `0x1000835D0`), wipes the tray from the top down as it runs and then calls `Jar::StopExplosion` (`0x100083A20`). The movie is 88 frames / 322 parts at the default **30 fps** (`PurblePlaceMovie::InitFromXml` `0x100087A89`) and its anchor is `/JarContainer` + `/JarMovieContainer` = `0,-110`. After a win the tray is empty: `s6-burst-3200.png`, `s6-won.png` |
| `pple-view --pairs` mismatch reveal | `Card::DoMatcher`'s mismatch branch is `Card+0x3C = Board+0x1C4` on the **second** card, with `Card+0xF8` left holding the first, and `Board::XMLConstants` (`0x100089FDC`) stores `/RevealDuration` **raw**, so the shipped 8 is a tick value, not seconds. It starts when `Card::OnReveal` (`0x10007FA6B`) runs `DoMatcher`, i.e. after the flip, and `Card::Update`'s tail (`0x10007DE08`) flips the pair back on the tick after the counter passes zero: 10 + 9 = **19 ticks (633 ms)** from the second click. `Card::DoCardClick` (`0x10007CD4C`) has no board-wide lockout, so every pair counts down on its own. `s6-miss-strip.png`: face up at 300 and 500 ms, flipping back at 700, backs at 900 |
| `pple-view --pairs` time bar (section 23.6) | Drawn from the board's own clock by `App::time_bar_state()`: segment `i` is lit while `i >= (total - left) / (total / 226)` and the bar swaps to `<TimeBarRed>` below 900 ticks. The frame loop compares that state before and after each tick, so a timed board keeps updating while nothing else moves — an idle `Intermediate,0` board now differs between 2 s and 5 s by 41,807 bytes where the previous commit was byte-identical |
| `pple-view --pairs` click handling (section 23) | A click while a mismatched pair is still face up is accepted, exactly as `Card::DoCardClick` does: the board holds **one** card waiting for a partner (`Board+0xB0`) and one `Card+0x3C` countdown per pair, and a click on a card's `FaceAnimation` node turns a face-up card back down and clears that slot. `--spam-check` asserts all of it: 17/17 PASS, and the 90-click storm on Advanced's 8x8 board peaks at **19 cards face up** against a 19-tick window, where a single restarted board-wide timer peaks at 48 and fails 8 of the assertions. Captures `s23-mismatch-third.png`, `s23-extra-card-stays.png`, `s23-taken-back.png` |
| `pple-view --menu --loading` | `Engine_SetBackMode`'s `UI\PurblePlace\Loading.xml` on `Loading.jpg` with the two `LOADING...` labels, then the menu. `s6-loading.png` |
| `pple-view --menu` clouds | `PurblePlaceManager::Init` (`0x10007124B`) builds `UI\PurblePlace\CloudsMovie.xml` into a `Base` created as a child of the Background sprite and puts it at `-390,-300`: **1016 frames at 10 fps**, loop off (`0x10007131E`), so it drifts once and rests. Drawn between the Background and the Foreground, which is where that child sits: `s6-clouds-strip.png` differs only in the sky band between 0.2, 12 and 24 s |
| `pple-view --difficulty` | the in-canvas Select Difficulty screen: `difficulty: Select Difficulty, 3 option(s)`; the classroom background, the waiting Professor and a green `Tips\Styles\Green` panel at `180,36 440x320` with MUI 213/214/219 and three `251x60` option buttons at `274,146+68i` carrying MUI 215/216/217 split into name and grid description. `s11-picker3.png` |
| `pple-view --menu --script-click` Professor intro | `100,180;400,175;wait:4400;700,200;wait:600;700,210` logs `tip ProIntro: "How To Play" at 615,150 175x123`, `tip ProIntro: closed`, `tip ProIntroNext: "How To Play" at 615,120 175x149`, `tip ProIntroNext: closed` — the deal-in, then `Pro_Enter` (11 frames at 12 fps) → `DisplayIntro`'s two bubbles → `Pro_Idle`, which is `Professor::Update`'s case 5. `s11-intro.png` |
| `pple-view --pairs` win and lose panels | `--pairs-solve` and `--pairs "Intermediate,0" --pairs-ticks 6300` now end in the in-canvas panel that replaces `DialogBoxMCE` 221: `tip ProWin3: "You Won!"` then `result panel: WON, score 410, 2710 -> Rank: Purble Master`, or `tip ProLose2: "Lost Game"` then `result panel: LOST`, with `Game Points: N`, `Final Score: N`, the `RANKS.XML` title and `New Game` / `Change Difficulty` / `Main Menu` on `251x60` buttons at `274,208+66i` inside a `200,80 400x336` panel. The panel needs a `--shot-delay` of about 4 s (win) or 9 s (lose): the movie, the bubble's `<Duration>` and `Pro_Exit` all run first. Both panels highlight their buttons on mouse motion, like the chooser. `s11-win3.png` |
| `pple-view --pairs` between-boards tip | `--pairs "Intermediate,0" --pairs-solve` logs `tip ProIntLevelChange: "Board Finished" at 615,125`; Advanced logs `tip ProAdvLevelChange`. `Jar::StopExplosion` (`0x100083A20`) picks the name from `Round+0x0C` (1 Intermediate, 2 Advanced) and sets `Professor+0x48 = 125` before `Board::NewLevel`. A scripted solve runs every level in one `build_pairs` call, so only the last level change's tip survives to the deferred rebuild |
| `pple-res consave` | On the file the game itself wrote (`ConcentrationScoresAndSettings.xml`, 65,707 bytes, 5,230 of them XML): **both the XML payload and the whole container re-emit BYTE-IDENTICAL**. `ConGeneralData::ToXml` `0x10007FC40`, `FromXml` `0x10007FE7C` |
| `pple-view --pairs-save` | Plays a Beginner round, updates the statistics and writes the file back through `pple::rgmh_write`; re-running loads the existing file and increments it (`played 2, won 2, tiles 50/50, avg 25`), and `pple-res consave` on the result is byte-identical both ways |
| `pple-view --pairs "Intermediate,0" --pairs-ticks N` | 3000 ticks → 110 s left, bar half drained; 5500 → 27 s left and the `RedTimerBar` resource branch is selected (`left < 900`), whose lit segments are red (`s26-red-bar.png`); 6300 → `LOST`, `professor -> ProfessorLoseAnimation`, and the bar is empty. 226 segments, `TimeBarGreen`'s own `<FrameCount>`. Slice `i` draws frame `i` of its template (`ConUILoader::CreateTimeSlice` `0x10008C640` stores the index in `[r9+180h]`), so the bar is the tube its 226 one-pixel frames profile instead of a flat slab; the slices sit at `TimeBarBkg`'s node position + `(3,21)` minus their own registration, i.e. `(73, 25+i)` green and `(74, 25+i)` red, which 1:1 measurement against the running game confirms (tube `76..123 x 25..250` both sides). The bar background's own `(-3,-5)` registration moves only its own draw. The loss test is `Level+0x0C > 0 && Round+0x154 != 0 && Round+0x118 <= 0`, so Beginner — whose `Level+0x0C` is `30 * -1` — cannot lose. `Board::Update` (`0x10008A5C1`) stops the clock once the board is complete |
| `pple-view --pairs` level change | `--pairs "Intermediate,0" --pairs-click <36 cells>` clears the first board through the click handler, prints `board relaid out -> level 2/2`, re-deals the second `<Level>`'s deck and back art, and the meter goes from one hollow indicator to one filled + one hollow. `Jar::StopExplosion` (`0x100083A20`) is the engine's decision point: no next `<Level>`, or `Round+0x124 >= Difficulty+0x20`, means the round is won; otherwise `Board::NewLevel`. A scripted run takes `Card::OnHide`'s end state directly — the matched cells show their BlankCard and the tray takes the fill the landings would have given it |
| `pple-view --pairs` card flip | `--script-click "100,180;200,60" --shot-delay 60` captures the back collapsed to ~1/3 width, `--shot-delay 130` the face expanding — the two phases of `Card::UpdateFlipForward` (`0x10007EA20`) |
| `pple-view` hover (focus follows the mouse) | `UserInterface::ProcessMouseMove` (`0x100098498`) hit-tests the node under the cursor and calls `SetFocus` (`0x1000992E0`), which sends `Event_FocusLost`/`FocusGot` (`0x12`/`0x11`). Motion is routed on **every** screen (`move:x,y` is the scripted form), and one hit test sets all three targets, so exactly one node is focused. A card shows `highlite_outline.xml` (`Card+0xD0`) and, face up, the additive `card_glow.xml` (`Card+0xC8`) — `Card::HandleEvent` FocusGot `0x10007C46F`, `Card::OnReveal` `0x10007F849`, material 2 set at `0x10007BCE9` and read as "additive" by `PurblePlaceUILoader::SetNodeMaterial` `0x1000730B0`; a `NodeButton` (the four menu buildings, `ReturnToMainMenu`, the sneak peek tokens) switches to `<Animation>` 1 (`NodeButton::HandleEvent` `0x1000AAC92`) and draws that animation's **last** frame, which is where its frame animation settles (`NodeButton::Load` `0x1000AAFF0` builds it, FocusGot `0x1000AA985` resumes it): `ReturnToMainMenu`'s hover art has two frames, the first a near-copy of the idle art and the second the white highlight, so frame 0 alone was the "no hover effect" the owner reported. Measured same-timing diffs, one target each: card outline `(173,25)-(261,113)` 1,287 px, additive glow `(171,25)-(262,116)` 7,533 px, token 0 `(3,448)-(48,494)` 1,522 px, `ReturnToMainMenu` `(701,441)-(795,492)` **2,633 px, 1,488 of them over 100** (2,148 px / none over 100 with frame 0), `--mce` `NewGame` 3,220 px, chooser "Beginner" label blue, score panel "New Game" label blue, menu building `(3,74)-(213,283)`. `s25-*.png`, `s25-glow-add-zoom.png`, `s26-hover-btn-2way.png` |
| `pple-view --pairs` card sneak icon | `Board::SelectSneakCard` (`0x10008B348`) flags one random card with `Card+0x18`, and `Card::InitUI` (`0x10007BDFA`) builds that card's `Card+0xA8` "SneakIcon" from `UI\Concentration\SneakPeakMiniIcon.xml` — 37x37 at `concentrationSheet29` 826,51, registration `(-1,-6)`. `Card::DoSneakIcon` (`0x10007DE60`) shows it while `Card+0x568 < 2`, so it is on the card face down and face up until the pair is matched. The reconstruction draws it at the card Base + `(1,6) * scale`: 1:1 measurement against the running game's own capture puts its gold at base `+1,+3` against the original's `-0.1,+2.6`, i.e. the same place within a pixel (`s26-eye-compare.png`) |
| `pple-view --mce` | 16 sprites and 3 labels instead of 14 and 0; `New Game` and `Exit Game` labels appear in the 51 px header strip and the clicks log `pairs: NewGame -> new round` and `pairs: ExitGame -> main menu`. Without `--mce` the same clicks do nothing |
| `pple-view --bench-build 20 --pairs "Beginner,0"` | a whole scene rebuild with every sheet cached: **18.464 ms/call** (21 calls, the extra one is the priming build). `--bench 300 --script-click "180,30;264,30"` with a mismatch running: the card layer, which replaced the per-tick rebuild, is **0.0807 ms/call primed** over 33 calls — about 230x cheaper per 30 Hz tick. The first call is reported apart because it also decodes the sheets the cards need (**9.277 ms**) |
| `pple-view --scene` container offsets | `TimeBarBkg` moves `3,21` → `63,4`, the jar to `15,315`, the peek background to `0,398` and the token rects to `3,448` / `47,448` / `93,448`, the Professor's movie anchor to `295,70`; `CONCENTRATIONGAME.XML` renders as the shipped screen (`new-conc.png` next to the game's own 256x192 thumbnail) |
| `pple-view --scene` power-of-two padding | `<PlayArea>`'s `148,0 800x500` now lands on a 1024x512 texture, so the 800x496 PNG is not stretched; the blue board's right edge lands at screen x **588.9**, which is where the game's own screenshot puts it (589). The unpadded reading gives 661.6 |
| `hidpi\rebuild_x2.py` split-page geometry | Rebuilding each split sheet from its four x2 pages (page `p` at `((p%2)*w, (p/2)*h)` of a twice-size canvas, 1:1) and halving the result reproduces the 1x sheet with a mean absolute error of **1.7–3.8** on Sheet1/2/12/13/18, quadrants included, against **77–111** for the wrong quadrant. Sheet18's 355x23 and Sheet13's 119x463 fourth pages are top-left-aligned crops of their quadrant, exactly as `Material::Render`'s `1024 / pageDim` factor reads them |
| `pple-view --hidpi` | The whole menu, loading screen, chooser and Pairs board from the engine's `g_bDoubleDPI` resources: **212 sheet loads, 0 fallbacks** across the 21-command 2x matrix, all 66 output assertions still PASS, and the 2x logs match the 1x ones apart from the `--sprite` sheet note and the coverage diagnostic (<=6 points, one 1-px-tall rect flipping). Edge energy on the deterministic stills **x1.14–x1.28** (x0.98 for the flat `loading` photograph), worst 32x32 block difference **28/255** between 1x and 2x where a clipped or missing sprite would light one block up. Cost: peak working set 110.8 -> 151.0 MB (menu) and 117.7 -> 168.4 MB (Beginner), one frame 875 -> 883 ms and 820 -> 1101 ms, cached full build 17.842 -> 32.158 ms/call. `s28-compare.py`, `s28-compare-shots.py`, `s28-perf.ps1` |
| `pple-view` mid-run DPI switch | `s28-switch-check.ps1`: 1200x830 guesses 1x and switches up to 2x (measured 1.512), a 1400x850 `--mce` board guesses 2x and switches down to 1x (measured 1.408, past the 1.425 hysteresis); in both directions the frame after the rebuild is **pixel-identical** to the frame a run started in that mode draws |

Viewable items: `PurblePlace.dll` 211 (67 PNG + 71 CAB + 73 JPEG), `PurblePlace2.dll` 24,
`CardGames.dll` 65 (27 + 14 + 24).

### Session 6 final regression run

`C:\re\traces\cpp\s7-verify.ps1` runs **29 commands**, all exit 0;
`s7-assert-verify.py` checks **62 output assertions**, all PASS. This includes the handoff's
asset/audio/rules/settings/menu/navigation/loading/solve/loss/scene/MCE/match/mismatch
commands, the new sneak fixture, the 5500-tick threshold and trace-directory save roundtrip.
The deterministic 36-cell `--pairs-click` level-change check also passes: level 2/2,
2 meter indicators at 665,40, step 55 (`s7-level2.log`).

Measured Release timings: **17.899 ms/call** for 21 full builds; **0.0835 ms/call primed**
for 34 card-layer refreshes, first 9.2018 ms, with only one full build in the 300-frame run.
No per-tick rebuild was added.

Final visible-change comparisons are in `C:\re\traces\cpp`: `s7-jar-side-by-side.png`,
`s7-burst-side-by-side.png`, `s7-tokens-side-by-side.png`, `s7-peek-side-by-side.png`.
The shipped thumbnail is a **static reference**, not an original animation capture.
`s7-peek-strip.png` records the movie/reveal/flip-back sequence. Intermediate's real token
click logs **6300 → 5985 ticks**, and its capture is `s7-peek-cost.png`.

Correction to the handoff's Advanced solve expectation: `--pairs-solve` (like `--pairs-click`)
intentionally skips jar animation, so it logs GumButton/GumDrop_Points but **not** a GumDrop
burst. The animated burst is verified through normal `--script-click` (`s7-burst-script-3200.png`),
and the GumDrop resource separately through `--movie --frame 30`: 88 frames/30 fps/322 drawn,
0 unresolved (`s7-gum-burst.log`).

## Initial sneak-token artwork correction

The playable scene hides `SneakPeekTokenAnimate` (an unused XML template) and
`SneakSparks` (initially hidden by the original engine). They previously drew an
extra eye token and sparkle above the first socket. The three socket positions and
starting counts are unchanged: Beginner 2, Intermediate 1, Advanced 0. Thus the
empty third Beginner socket is intentional, not a missing token.

Verified with `s8-token-*` captures in `C:\re\traces\cpp`: initial 2/1/0 tokens,
one token after a Beginner click and 2300 ms, and no extra eye or sparkle.
All three `--sneak-check` fixtures pass; five rebuilds retain 6 Beginner static
sprites (previously 8). Generic scene-only rendering is unchanged. See notes 20.5.

## Behavior-preserving viewer split

The viewer module split preserves all 32 moved lambda bodies token-for-token
(after expanding the setup helpers). Command-line flags, draw order, game rules,
cache ownership and the 30 Hz update path are unchanged. All source files in the
viewer directory are compiled normally; this is not a textual `.inc` split.

Session 9 evidence under `C:\re\traces\cpp`:

- Full 29-command matrix before/after: exit 0; 62 output assertions pass. The old
  fixed benchmark expectation of 34 card refreshes fails on the unchanged baseline
  (33 measured), so the comparison now checks the metric and a positive refresh
  count; the one-full-build invariant still passes.
- All 29 normalised command logs agree, excluding performance timings and the
  generated save's wall-clock date/hash. Each save still round-trips byte-identically.
- 12 of 15 full-matrix capture pairs are pixel-identical; three timed match/mismatch
  captures differ with real-time animation. No fixed frame-equality claim for those.
- Seven additional mode comparisons agree in both output and pixels: initial
  Beginner/Intermediate/Advanced jar/token panels, one spent token, still sprite,
  still movie and image browser. Four panel crops and three complete captures.
- Warm full-build average: baseline 18.686 ms/call, refactor 18.007 ms/call (21 calls
  each). Primed card-layer cost 0.0826 vs 0.0886 ms/call; these are samples, not a
  performance improvement claim. Final standard build is also verified.

See the [module map](src/viewer/README.md) and notes section 20.6. No gameplay
features or existing known-gap fixes were included in this structural milestone.

## Session 28: the game's own double-DPI (x2) art

`--hidpi`, `--no-hidpi`, and an automatic rule (x2 from a 1.5x window scale, back to 1x below
1.425x) draw the whole menu, the loading screen, the difficulty chooser and Purble Pairs from the
x2 resources the engine loads when `g_bDoubleDPI` is set. The layout, the hot-spots, the hit
tests and the destination rectangles are untouched; only the texels read change. The rule and its
evidence are in [the double-DPI section](#the-double-dpi-x2-art).

Session 28 evidence under `C:\re\traces\cpp` (the four matrices and the comparison that produced
these numbers are `s28-verify-all.ps1`, `s28-run.ps1`, `s28-extra-probes.ps1`,
`s28-compare.py` and `s28-asserts.py`):

- **The 1x path did not change.** A binary built from the pre-change source
  (`build\pple-before`) and the new binary forced to 1x (`--no-hidpi`) agree in every normalised
  log line and in all 16 captures that two runs of the same 1x binary also agree on; four
  timed match/mismatch captures are excluded as wall-clock sensitive by that control.
- **66 output assertions pass in all four runs** (pre-change, 1x, 1x again, 2x): geometry,
  scores, ranks, hit tests, the sneak fixtures, the settings round trip and the level changes are
  all mode-independent.
- **2x keeps the same structure**: every normalised log line still agrees, apart from the
  `--sprite` probe, which prints the sheet it drew (1024x128 1x vs 2048x256 2x), and the
  diagnostic coverage percentages, which sample the 2x art instead and move by at most 6 points
  on five frames (a rect 1 px tall, `TimeBarGreen`, can land inside a grid cell in one mode and
  outside it in the other; nothing drawn changes).
- **No fallbacks**: 212 sheet loads across the 21 2x commands, every one from its x2 resources.
  The eleven sheets the menu and Pairs reference (Sheet1, Sheet2, Sheet21, Sheet12, Sheet13,
  Sheet18, Sheet29, Sheet30, Sheet31, Sheet32, Sheet34) all have an x2 form — five as four split
  pages, six as an `…x2.jpg`/`.bmp.cab` pair — and so do the images the screens draw directly
  (`PurblePlaceBackground`, `PurblePlaceForeground`, `Loading`, `GameBoard`, `Background`,
  `MiniStarExplosion`, the tip-style PNGs and the CardGames backgrounds).
- **Pixels**: 26 captures are redrawn from the x2 art. On the deterministic stills the edge
  energy rises by **x1.14 to x1.28** (`scene` x1.27, `solve-Beginner` x1.21, `mce` x1.20, `save`
  x1.21, `menu` x1.28; the `loading` screen is a flat photograph and measures x0.98). A 32x32
  block sweep of 1x against 2x stays at or below **28/255** where resampling alone explains it
  (limit 40), which is what rules out a clipped, missing or displaced sprite.
- **Switching mid-run** rebuilds the screen and lands on the exact frame a run that started in
  that mode draws. `s28-switch-check.ps1` drives both directions on a static Beginner board:
  1200x830 guesses 1x and switches **up** to 2x, and a 1400x850 `--mce` board (design height 547)
  guesses 2x and switches **down** to 1x; both final frames are **pixel-identical** to the
  from-scratch run.
- **Cost**: peak working set 110.8 -> 151.0 MB for the menu and 117.7 -> 168.4 MB for a Beginner
  board; one rendered frame takes 875 -> 883 ms for the menu and 820 -> 1101 ms for Pairs. With
  every sheet already cached, a full `build_pairs` is 17.842 -> 32.158 ms/call and the primed card
  layer 0.0801 -> 0.0863 ms/call, so the 30 Hz path is not the part that pays.

### Session 40: Purble Shop

- `ctest --test-dir build/pple-res`: **3/3** (cakes model, resource animation, `shop-rules`).
  [tests/shop_test.cpp](tests/shop_test.cpp) checks `Face::Compare`'s multiset rule (including the
  consumed duplicate and the absent colour), the per-difficulty setup over 200 seeds each, the
  selection toggle and its bounds, the score arithmetic (`100 + 10*rcrf` and `+ 5*rcwf` on
  Advanced), the duplicate-guess refusal and both end conditions.
- `tests/cakes_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR`: **23 runs PASS** (all three Comfy
  Cakes difficulties in both DPI modes with solves and normal clicks, the menu roundtrip, the
  difficulty keys, and the three Pairs solves), i.e. the menu hot-spot, chooser and loop changes
  left the other two games alone.  Evidence `C:\re\traces\cpp\cc\s40-general`.
- Purble Shop captures: `C:\re\traces\cpp\bs\shop-{beginner,inter,adv}.png` (the three rooms),
  `shop-guess.png` (a Beginner win: 3 right, score 130, the shelves' green checks and red Xs),
  `shop-inter-guess.png` (one judged guess with its history slot), `shop-inter-lose.png` (six
  judged guesses, six slots with their own thermometer frames and numbers), `shop-adv-guess.png`
  (Advanced's two counts: 1 right colour/right feature and 1 right colour/wrong feature, score
  115), `shop-chooser.png` and `shop-from-menu.png` (the menu's house -> the chooser -> a round).
- Audio: the viewer reported `audio shop wasapi: 12/12 events decoded`, and a scripted win queued
  `BAP_CURTAIN_CLOSE`, `BAP_FEATURE_INTRO`, the three picked items' own `<SoundID>` events,
  `BAP_FACE_INFLATE`, `BAP_FEATURE_WIN`, `BAP_CURTAIN_WIN_OPEN` and `BAP_WIN_GAME`.
- Session 41 (history scrolling and check-box tagging): `ctest` **3/3** and
  `tests/cakes_regression.py` **23/23** again.  Advanced with eight judged guesses through
  `--shop-guess` then `--script-click '48,430;48,430;48,430;772,430'` walks the window
  `3..8 -> 2..7 -> 1..6 -> 2..7` and ghosts an arrow at alpha 32 exactly where
  `HistoryWindow::UpdateSlots` does (`shop-adv-history.png`, `shop-history-scroll.png`).  Four
  clicks on one check box cycle wrong -> right -> untagged -> wrong, and
  `shop-inter-tag.png` / `shop-tags.png` show the tags.  The evidence also corrects an earlier
  claim: `<OverrideBB>` is dead data (`LoadButton` 0x1000737B0 reads no hit box and the string does
  not exist in either DLL); `RenderManager::HitTestTopLevel` (0x10008F3D0) walks the render list
  backwards, so a button's own rect is what a click tests.

### Session 43: shared dialog/button seams

- [tests/ui_panels_regression.py](tests/ui_panels_regression.py) `BUILD_DIR SAMPLE_DIR TRACE_DIR`:
  **42 viewer runs PASS**. Checks the three games' choosers at 800x496, 1000x700 and 1400x900,
  button-only focus differences, selection clicks, all three win panels, and white/red help tips,
  in both DPI modes. Join probes are above the labels; they reject the old filter-gutter stripe.
- Baseline at 1000x700: a button join pixel `(358,230)` was RGB **194,194,194**; with corrected
  frame UVs it is **255,255,255**. Before/after captures and the matrix are under
  `C:\re\traces\cpp\ui-clean`. Layout and click rectangles are unchanged.
- Existing checks after the fix: `cakes_ui_regression.py` **30 PASS**, `cakes_regression.py`
  **23 PASS**, and CTest **3/3 PASS**. No game rules or per-tick rendering paths changed.

## Known gaps

* ~~The exact on-disk geometry of a split sheet's **fourth** `x2` page~~ — **derived and used in
  session 28** (see [the double-DPI section](#the-double-dpi-x2-art)): a page is a
  top-left-aligned crop of its quadrant at 2x, so placing page `p` at `((p%2)*w, (p/2)*h)` of a
  canvas twice the virtual size rebuilds the sheet to a mean absolute error of 1.7–3.8, including
  the trimmed pages (SHEET18's 355x23 and SHEET13's 119x463, whose quadrants really do hold only
  that much art). `Material::Render`'s `1024 / pageDim` factor is how the engine reads a trimmed
  page in place rather than stretching it.
* The generic `--movie --frame N` viewer remains a still. Menu clouds, jar burst,
  sneak peek and Professor idle/win/lose **are** free-running on their screens, at the
  movie's frame rate. Professor anchors at `/ProfessorContainer` `295,70`
  (`Professor::InitUI` `0x10008BD94`). Rotation pivots about each part's destination centre;
  the engine's pivot is not established.
* The `<Justify>` meaning is now read from `TextRenderer::GetTextRenderInfo`
  (`0x1000A02C0`) rather than guessed: it is a `DrawText` flag mask (see section 15 of
  `D:\Development\re\notes\purbleplace.md`).
* WMA audio **plays**. `SDL3_mixer` was the plan in the handoff but has no WMA decoder (its
  set is aiff/au/flac/fluidsynth/gme/mp3/opus/raw/sinewave/timidity/vorbis/voc/wav/wavpack/xmp),
  so decoding goes through Media Foundation in `pple_audio`; see section 16 of
  `D:\Development\re\notes\purbleplace.md`. Not done: the manifest's `<Buffers>` concurrency
  limit is ignored, and `<Music>` is empty in both manifests.
* PNG interlacing (Adam7) is rejected with a clear error; no shipped asset is interlaced.
* The three games: Purble Pairs is **playable** (`pple-view --menu`, or headless with
  `pple-res pairs --autoplay`). Comfy Cakes plays a whole round (`--cakes [Beginner|
  Intermediate|Advanced]`, or the menu's Comfy Cakes house): the room, its engine draw order, its
  hover, the rules, the score, the Chef's six movies, and each tool machine's own animation with
  the ingredient it drops into the cake.  Purble Shop (internally Build-a-Purble) plays a whole
  round too (`--shop [Beginner|Intermediate|Advanced]`, or the menu's Purble Shop house): the room,
  the cabinet with one visible variant per colour, the guess purble, the counts, the history strip,
  the Beginner check-box marks, the reveal, the round's own sounds and the shared end-of-round
  panel.  The history strip scrolls (its arrows' own sounds and ghosting) and
  Intermediate/Advanced items can be tagged.  Not done there yet: the curtain/stage intro
  animations, the win and inflate animations, the game's own tips and the save files; see sections
  40 and 41 of `D:\Development\re\notes\purbleplace.md`.
  The card deck, board geometry,
  match rule, scoring, placement, container parenting, flip animation, sound map, progress
  meter, multi-level rounds, special cards, sneak peek, per-level card back, the jar's tray
  coins and flying coins, the burst movie, the match animation, `CalcEndGame`'s rank and the
  settings save file are reversed; see sections 14, 17, 18, 19 and 20 of
  `D:\Development\re\notes\purbleplace.md`. Not implemented:
  * ~~**`ConcentrationGame::SaveGameState`** (`0x100082B40`)~~ — **done in section 27.6**:
    `--pairs-state PATH` writes the round on `ReturnToMainMenu` (RGMH header, a 256x192 PNG
    thumbnail, a UTF-16LE XML payload) and resumes it, thumbnail included, when the file already
    holds one; `ConcentrationBoard::restore` puts the dealt board back. The shipped file name
    (`Purble Pairs.PurblePairsSave-ms`) and `ConRoundData::ToXml`'s full `<RoundData>` schema are
    still not reproduced — the payload is the reconstruction's own, with `TIPS.XML`'s element
    names — and the statistics file itself still round-trips byte for byte (`pple-res consave`);
  * ~~**the rest of the card's own effects**~~ — **done in section 27.4**: `Card_shadow.xml`
    (`Card+0xC0`) under every card that has been turned, `tileglow.xml` (`Card+0xE8`) and
    `tilecover.xml` (`Card+0xF0`) on the matched bonus tile, and the `/BonusTileAnimation` movie
    (`Card+0xE0`) while its counter runs. `card_glow.xml` and `highlite_outline.xml` **are** drawn
    as the hovered card's focus art, and so is `SneakPeakMiniIcon.xml` (`Card+0xA8`, the sneak
    card's eye mark). The four `SparkleSprite_y` sparks are drawn as the match's explosion;
  * **one shipped card id has no face art in the box, and the reconstruction reproduces that**:
    `UI\CONCENTRATION\CARDS.XML`'s id 28 points at `UI\Concentration\TwoLayerRectangleFlip.xml`,
    which no resource in any of the three DLLs carries — the sprite that exists is
    `UI\CONCENTRATION\TWOLAYERRECTFLIP.XML`. `XmlManager::GetXml` (`0x100092104`) compares the
    requested path exactly, so the engine cannot load it either and the four id-28 cells of an
    Intermediate board draw nothing (`4 card sprite(s) did not resolve`). Filling it from
    `TWOLAYERRECTFLIP.XML` would be a deliberate deviation from the shipped data, so it is left
    as the box has it;
  * ~~**the two particle emitters**~~ — **done in section 27.5**: `Emitter_gold.xml` and
    `Emitter_white.xml` are emitted from each flying coin's own position, `(-10,-10)`, once per
    tick of the flight, with `ParticlePaperCreation`'s velocities, `ParticleUpdate`'s
    `life * 1275` alpha and both `SparkleSprite` variants;
  * **the card deal-in's own shadow**: the "IntroImage" is drawn as the card's back art at the
    growing scale rather than through its own node. The first card of a new board also deals
    instantly when the board is rebuilt for a new `<Level>` (`keep_state`), where the shipped
    `Board::NewLevel` runs `Card::InitUI` again and would deal the new board in;
  * **`<TipManager><DisabledTips>`**: `pple::ConSettings` parses and re-emits the list, but the
    viewer does not consult it — every tip is enabled, and there is no Options screen to turn one
    off. The shipped build also gates tips on `g_pPurblePlaceManager+3*8+12`;
  * the Mixer's once-per-level guard `Board+0x1DC` is approximated by `Card::DoMixerCheck`'s
    `> 3` card gate alone;
  * **the card's state machine is coarser than the engine's**. `Card+0x568` (`+346` dwords) holds
    0/1/2/3/4/5/6/8 in the shipped build, and `Card::HandleEvent` (`0x10007C1F4`) lets a state-4
    card — one that is turning back down, its back art visible again — start its flip forward
    again. The reconstruction keeps `CardFace` plus the viewer's single flip for both directions,
    so a click on a card that is still turning is refused instead. Two related simplifications:
    `Board::Update`'s completion pass only force-matches cards in state 0, where the
    reconstruction takes every card that is left (and clears the card waiting for a partner with
    it), and `mismatch_ticks()` reports -1 during the flip-forward, before `Card::OnReveal` arms
    `Card+0x3C`;
  * ~~the Professor's `ProFlipNoMatchTimed` / `ProFlipMatchUntimed` tip~~ — **shown in section
    27.3**, but as an addition rather than a recovery: `Card::DoMatcher` (`0x10007D224`) arms the
    Professor for a *matched* pair and leaves him alone on a mismatch (sound `0x74` and a return
    at `0x10007D28D`), and `TIPS.XML` carries no mismatch tip, so the reconstruction raises the
    match branch's own tip at the point the wrong pair turns over;
  * `<NumCardTypes>`, which is read from the XML into `Level+8` and has no reader I could find.
    It agrees with the number of letter runs for Beginner (4) but not for Intermediate (stored 5,
    8 runs) or Advanced (`-1`, 9-12 runs);
  * `<NumStartTokens>` drives the token count and the sneak peek, but nothing else;
  * the fifth `<Back>` (Star) in `CardBacks.xml` is never loaded (`Board::Init`'s loop is four
    iterations), and `/animpath` and `/jarpath` have no reader anywhere in the image. The other
    four fields do: `/buttonpath` is the card back, `/imagepath` the jar coin, `/moviepath` the
    jar's burst movie and `/progpath` the meter indicator.
* The engine's own HUD text on an untimed round has no time bar, which the reconstruction
  reproduces (`TimeBar::InitUI` `0x10008538E` hides `/TimeBarBkg` when the difficulty is 0).
* 240 distinct references (464 occurrences) do not resolve — content variants that were never
  shipped and other games' backgrounds. The game's own loader tolerates misses.
