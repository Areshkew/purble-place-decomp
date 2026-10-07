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
```

Controls: **left click** a card to flip it, or a choice button to pick a difficulty and start the
next round; **click anywhere** to advance or dismiss the Professor's tip; **N** or **R** for a new
round, **ESC** to go back to the menu (and to quit from the menu), **SPACE** to toggle whole-pixel
scaling, **S** to write the current image as a PPM. `--mute` silences the sound effects. `--mui`
is optional; without it the labels fall back to the English text embedded in the XML, which is why
the title then renders in Comic Sans MS rather than Segoe UI. The difficulty chooser and the
end-of-round panel keep working without it: they carry the shipped `.mui` wording as their
fallback.

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
| `pairs <pe> [--difficulty D] [--level N] [--seed N] [--layout] [--autoplay] [--sneak-check] [--wild-early]` | Builds a real Purble Pairs board from the shipped data; `--layout` prints every cell, `--autoplay` plays the whole round out, `--wild-early` spends the Wild Card on the first partner before the autoplay does (the regression test for the twin it clears) |
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
pple-view <pe>... [--menu] [--difficulty] [--resource NAME] [--sprite XML] [--scene XML] [--movie XML]
                  [--movie-name NAME] [--anchor X,Y] [--frame N] [--state N]
                  [--pairs [D,L,S]] [--pairs-reveal] [--pairs-click I,J] [--pairs-ticks N]
                  [--pairs-solve] [--pairs-save PATH] [--mce] [--no-container-offsets] [--mui PATH]
                  [--intro] [--no-intro] [--no-tips]
                  [--loading [MS]]
                  [--play NAME] [--volume N] [--mute]
                  [--script-click "x,y;x,y;wait:MS"] [--shot-delay MS] [--bench N] [--bench-build N]
                  [--list] [--shot out.ppm] [--save out.ppm] [--width N] [--height N]
                  [--font PATH] [--all]
```

`--menu` is the real main menu and the entry point: `PurblePlaceManager.xml` (which lives in
`PurblePlace.dll`) for the background, the `Foreground` sprite (which paints the four icons), the
title labels and the four `<NodeButton>` hot-spots, with each button's `<Animation>` 1 as the
hover art. Clicking the Purble Pairs hot-spot opens the in-canvas **Select Difficulty** screen
(see below) and its three buttons start the game; the in-game `ReturnToMainMenu` button comes
back; `ESC` goes back too, and `N`/`R` start a new round at the same difficulty.
`--difficulty` starts on that chooser directly. `--mce` additionally draws the Media Center
build's `New Game` / `Exit Game` header buttons — the shipped executable *deletes* those five
nodes when `g_bMediaCenter == 0` (`ConcentrationGame::LoadGameUI` `0x10008122B`), so they are
absent by default, as they are in this build.  `--no-container-offsets` turns off the engine's
HUD re-parenting described below.

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

Two engine details the bubbles follow: a tip with `<NoTimeout>` waits for a click and the
`<CaptureGlobalClick>` default is what makes the first click anywhere advance it, while a tip
with a `<Duration>` expires on its own and does not swallow clicks. `--no-tips` draws no bubbles;
the Professor still animates and the win/lose panel still appears.

`--script-click` entries may be `wait:MS`, which is how the intro chain is checked without a
mouse: `--menu --script-click "100,180;400,175;wait:1500;700,200;wait:600;700,210"` picks
Beginner, waits for `Pro_Enter`, then clicks the two bubbles.

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
mouse uses, so menu → game → menu and the card flip can be verified without a mouse;
`--shot-delay MS` waits that long before capturing, which is what catches a flip mid-animation.
`--pairs-solve` plays the whole round out — every `<Level>` of the difficulty — by clicking
matchable pairs (equal `<id>`, or the Wild Card against anything) through the same handler the
mouse uses, and `--pairs-ticks N` advances the engine's fixed 30 Hz step N times first, so the
time bar, the red threshold and the loss state are reachable immediately. `--bench N` renders N
frames with vsync off and reports the card-layer cost; `--bench-build N` reports the cost of N
whole scene rebuilds.

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

`--movie` renders a `<Movie>` skeletal animation: `<Library frameCount=>` names the length,
each `<ImageInstance name=>` is a part resolved through `<ImageDef xml=>` to an ordinary sprite
node definition, and each `<Frame num= x= y= rotation= xscale= yscale= alpha=>` is a keyframe.
An omitted attribute holds its previous value and a property with two or more keys is linearly
interpolated, which is what `<Frame num="1"/>` means. `--anchor X,Y` places the movie's own
coordinate space. In `--scene` mode every movie instance is resolved and reported, positioned
ones are drawn, and `--movie-name NAME --anchor X,Y` draws one that the document does not place
(the Concentration screen's nine `Professor*`/`SneakMovie`/`FireWorks` slots are all of that
kind).

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
  keyframe tracks with hold and linear interpolation.
* `wma.cpp` — WMA decoding through Media Foundation: a read-only in-memory `IStream`,
  `IMFSourceReader` to 16-bit PCM, and a RIFF/WAVE writer. SDL3_mixer has no WMA decoder, so
  this is the decoder.
* `mui.cpp` — `.mui` `RT_STRING` tables, the UTF-16/UTF-8 helpers, and the localiser hook that
  `localized_default` consults before falling back to the English text in the data.
* `sha256.cpp` — digest helper.
* `view_main.cpp` — 8-line viewer entry point. The former 4,026-line implementation is
  split into 21 responsibility-focused C++ translation units under `src/viewer`;
  the largest is 388 lines. Start with the [viewer module map](src/viewer/README.md),
  then read only the module for the feature being changed. `App` owns session state;
  the board rules remain in the SDL-free core library. `tips.cpp` owns the `TIPS.XML`
  bubbles and their style, `professor.cpp` the Professor's state machine, and
  `overlays.cpp` the two screens that replace a shipped dialog (the difficulty chooser
  and the end-of-round panel).

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
| `pple-view --pairs` jar burst | `Jar::Update` (`0x10008373C`) plays `Con_BoardCleared` when the last coin lands, shows the level's `/moviepath` (`Jar::LoadMovies` `0x1000835D0`), wipes the tray from the top down as it runs and then calls `Jar::StopExplosion` (`0x100083A20`). The movie is 88 frames / 322 parts at the default **30 fps** (`PurblePlaceMovie::InitFromXml` `0x100087A89`) and its anchor is `/JarContainer` + `/JarMovieContainer` = `0,-110`. After a win the tray is empty: `s6-burst-3200.png`, `s6-won.png` |
| `pple-view --pairs` mismatch reveal | `Card::DoMatcher`'s mismatch branch is `Card+0x3C = Board+0x1C4` and `Board::XMLConstants` (`0x100089FDC`) stores `/RevealDuration` **raw**, so the shipped 8 is 8 ticks, not 8 seconds. It starts when `Card::OnReveal` (`0x10007FA6B`) runs `DoMatcher`, i.e. after the flip, and `Card::DoCardClick` (`0x10007CD65`) has no board-wide lockout. `s6-miss-strip.png`: face up at 300 and 500 ms, flipping back at 700, backs at 900 |
| `pple-view --menu --loading` | `Engine_SetBackMode`'s `UI\PurblePlace\Loading.xml` on `Loading.jpg` with the two `LOADING...` labels, then the menu. `s6-loading.png` |
| `pple-view --menu` clouds | `PurblePlaceManager::Init` (`0x10007124B`) builds `UI\PurblePlace\CloudsMovie.xml` into a `Base` created as a child of the Background sprite and puts it at `-390,-300`: **1016 frames at 10 fps**, loop off (`0x10007131E`), so it drifts once and rests. Drawn between the Background and the Foreground, which is where that child sits: `s6-clouds-strip.png` differs only in the sky band between 0.2, 12 and 24 s |
| `pple-view --difficulty` | the in-canvas Select Difficulty screen: `difficulty: Select Difficulty, 3 option(s)`; the classroom background, the waiting Professor and a green `Tips\Styles\Green` panel at `180,40 440x300` with MUI 213/214/219 and three `251x51` option buttons at `274,150+62i` carrying MUI 215/216/217 split into name and grid description. `s10-difficulty.png` |
| `pple-view --menu --script-click` Professor intro | `100,180;400,175;wait:1500;700,200;wait:600;700,210` logs `tip ProIntro: "How To Play" at 615,150 175x123`, `tip ProIntro: closed`, `tip ProIntroNext: "How To Play" at 615,120 175x149`, `tip ProIntroNext: closed` — `Pro_Enter` (11 frames at 12 fps) → `DisplayIntro`'s two bubbles → `Pro_Idle`, which is `Professor::Update`'s case 5. `s10-next-bubble.png`, `s10-intro-done.png` |
| `pple-view --pairs` win and lose panels | `--pairs-solve` and `--pairs "Intermediate,0" --pairs-ticks 6300` now end in the in-canvas panel that replaces `DialogBoxMCE` 221: `tip ProWin3: "You Won!"` then `result panel: WON, score 410, 2710 -> Rank: Purble Master`, or `tip ProLose2: "Lost Game"` then `result panel: LOST`, with `Game Points: N`, `Final Score: N`, the `RANKS.XML` title and `New Game` / `Change Difficulty` / `Main Menu`. `s10-win-panel.png`, `s10-lose-panel.png` |
| `pple-view --pairs` between-boards tip | `--pairs "Intermediate,0" --pairs-solve` logs `tip ProIntLevelChange: "Board Finished" at 615,125`; Advanced logs `tip ProAdvLevelChange`. `Jar::StopExplosion` (`0x100083A20`) picks the name from `Round+0x0C` (1 Intermediate, 2 Advanced) and sets `Professor+0x48 = 125` before `Board::NewLevel`. A scripted solve runs every level in one `build_pairs` call, so only the last level change's tip survives to the deferred rebuild |
| `pple-res consave` | On the file the game itself wrote (`ConcentrationScoresAndSettings.xml`, 65,707 bytes, 5,230 of them XML): **both the XML payload and the whole container re-emit BYTE-IDENTICAL**. `ConGeneralData::ToXml` `0x10007FC40`, `FromXml` `0x10007FE7C` |
| `pple-view --pairs-save` | Plays a Beginner round, updates the statistics and writes the file back through `pple::rgmh_write`; re-running loads the existing file and increments it (`played 2, won 2, tiles 50/50, avg 25`), and `pple-res consave` on the result is byte-identical both ways |
| `pple-view --pairs "Intermediate,0" --pairs-ticks N` | 3000 ticks → 110 s left, bar half drained; 5500 → 27 s left and the `RedTimerBar` resource branch is selected (`left < 900`); the current capture looks gold/yellow, so this is not a verified red-pixel change; 6300 → `LOST`, `professor -> ProfessorLoseAnimation`, and the bar is empty. 226 segments, `TimeBarGreen`'s own `<FrameCount>`. The loss test is `Level+0x0C > 0 && Round+0x154 != 0 && Round+0x118 <= 0`, so Beginner — whose `Level+0x0C` is `30 * -1` — cannot lose. `Board::Update` (`0x10008A5C1`) stops the clock once the board is complete |
| `pple-view --pairs` level change | `--pairs "Intermediate,0" --pairs-click <36 cells>` clears the first board through the click handler, prints `board relaid out -> level 2/2`, re-deals the second `<Level>`'s deck and back art, and the meter goes from one hollow indicator to one filled + one hollow. `Jar::StopExplosion` (`0x100083A20`) is the engine's decision point: no next `<Level>`, or `Round+0x124 >= Difficulty+0x20`, means the round is won; otherwise `Board::NewLevel`. A scripted run takes `Card::OnHide`'s end state directly — the matched cells show their BlankCard and the tray takes the fill the landings would have given it |
| `pple-view --pairs` card flip | `--script-click "100,180;200,60" --shot-delay 60` captures the back collapsed to ~1/3 width, `--shot-delay 130` the face expanding — the two phases of `Card::UpdateFlipForward` (`0x10007EA20`) |
| `pple-view --mce` | 16 sprites and 3 labels instead of 14 and 0; `New Game` and `Exit Game` labels appear in the 51 px header strip and the clicks log `pairs: NewGame -> new round` and `pairs: ExitGame -> main menu`. Without `--mce` the same clicks do nothing |
| `pple-view --bench-build 20 --pairs "Beginner,0"` | a whole scene rebuild with every sheet cached: **18.464 ms/call** (21 calls, the extra one is the priming build). `--bench 300 --script-click "180,30;264,30"` with a mismatch running: the card layer, which replaced the per-tick rebuild, is **0.0807 ms/call primed** over 33 calls — about 230x cheaper per 30 Hz tick. The first call is reported apart because it also decodes the sheets the cards need (**9.277 ms**) |
| `pple-view --scene` container offsets | `TimeBarBkg` moves `3,21` → `63,4`, the jar to `15,315`, the peek background to `0,398` and the token rects to `3,448` / `47,448` / `93,448`, the Professor's movie anchor to `295,70`; `CONCENTRATIONGAME.XML` renders as the shipped screen (`new-conc.png` next to the game's own 256x192 thumbnail) |
| `pple-view --scene` power-of-two padding | `<PlayArea>`'s `148,0 800x500` now lands on a 1024x512 texture, so the 800x496 PNG is not stretched; the blue board's right edge lands at screen x **588.9**, which is where the game's own screenshot puts it (589). The unpadded reading gives 661.6 |

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

## Known gaps

* The exact on-disk geometry of a split sheet's **fourth** `x2` page (SHEET18's is 355x23 where
  the other three are ~1024x1024). The page path is only reachable with `g_bDoubleDPI` set, and
  `Material::Render` (`0x1000A96E0`) rescales each page by `1024 / pageDim`, so the engine is
  expected to absorb it. Not derived; not needed for the normal-DPI path.
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
  `pple-res pairs --autoplay`). Comfy Cakes and Purble Shop are not started; their menu
  hot-spots are drawn and clickable and say so when clicked. The card deck, board geometry,
  match rule, scoring, placement, container parenting, flip animation, sound map, progress
  meter, multi-level rounds, special cards, sneak peek, per-level card back, the jar's tray
  coins and flying coins, the burst movie, the match animation, `CalcEndGame`'s rank and the
  settings save file are reversed; see sections 14, 17, 18, 19 and 20 of
  `D:\Development\re\notes\purbleplace.md`. Not implemented:
  * **`ConcentrationGame::SaveGameState`** (`0x100082B40`), which writes the resumable round to
    `Purble Pairs.PurblePairsSave-ms`. Its `<RoundData>` fields are known
    (`ConRoundData::ToXml` `0x10008035C`) but the document is not written, and the board
    thumbnail the game stores beside the statistics is not produced either. The statistics file
    itself **does** round-trip byte for byte (`pple-res consave`);
  * **the rest of the card's own effects**: `card_glow.xml`, `highlite_outline.xml`,
    `tileglow.xml`, `tilecover.xml`, the `/BonusTileAnimation` movie and `Card_shadow.xml` are
    all created by `Card::InitUI` (`0x10007B8D8`) and none of them is drawn. The four
    `SparkleSprite_y` sparks *are* drawn, as the match's explosion;
  * **the two particle emitters** `Emitter_gold.xml` and `Emitter_white.xml`, which
    `ArcBehavior::Init` (`0x100083E48`) builds with `ParticleUpdate` (`0x100084678`) and
    `ParticlePaperCreation` (`0x100084424`) as callbacks;
  * **the card deal-in**: `Board::Update` (`0x10008A4F0`) keeps the board inert until every card
    has left `Card+0x1384`'s state 6 and then sets `Board+0x1E8 = 1`, which `Card::HandleEvent`
    (`0x10007C4A3`) tests before it acts; the animation itself is `Card_shadow.xml` (`Card+0xC0`),
    which is not drawn. The reconstruction sets the board playable at once, so a card can be
    clicked while the Professor is still walking in, where the shipped build waits out the deal;
  * **the tip's style selection**: `<Type>` picks one of the three style documents
    (`TipManager::parseXml` `0x1000934C3`: `Intro` → 0, `Error` → 2, anything else → 1) and the
    reconstruction always draws the green set, so the one `<Type>Error</Type>` tip (`BlankCard`)
    is green rather than red. The three style documents' own names are assigned at runtime by
    the manager, so the static image does not say which index is which set;
  * **`<TipManager><DisabledTips>`**: `pple::ConSettings` parses and re-emits the list, but the
    viewer does not consult it — every tip is enabled, and there is no Options screen to turn one
    off. The shipped build also gates tips on `g_pPurblePlaceManager+3*8+12`;
  * the Mixer's once-per-level guard `Board+0x1DC` is approximated by `Card::DoMixerCheck`'s
    `> 3` card gate alone;
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
