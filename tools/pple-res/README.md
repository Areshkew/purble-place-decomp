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

# The real main menu — click the bell house to play Purble Pairs.
.\build\pple-res\pple-view.exe @game --menu --mui $m

# Straight into the game, no menu.
.\build\pple-res\pple-view.exe @game --scene "UI\CONCENTRATION\CONCENTRATIONGAME.XML" --pairs "Beginner,0" --mui $m

# Intermediate is the first timed difficulty, so it is the one that shows the time bar.
.\build\pple-res\pple-view.exe @game --scene "UI\CONCENTRATION\CONCENTRATIONGAME.XML" --pairs "Intermediate,0" --mui $m
```

Controls: **left click** a card to flip it, **N** or **R** for a new round, **ESC** to go back to
the menu (and to quit from the menu), **SPACE** to toggle whole-pixel scaling, **S** to write the
current image as a PPM. `--mute` silences the sound effects. `--mui` is optional; without it the
labels fall back to the English text embedded in the XML, which is why the title then renders in
Comic Sans MS rather than Segoe UI.

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
| `pairs <pe> [--difficulty D] [--level N] [--seed N] [--layout] [--autoplay]` | Builds a real Purble Pairs board from the shipped data; `--layout` prints every cell, `--autoplay` plays it out |
| `strings <file.mui> [--id N] [--grep TEXT] [--limit N]` | Dumps the `RT_STRING` table by id |
| `muicheck <file.mui> <pe>...` | Compares every `\|id\|context\|English` reference in the XML resources against the MUI |
| `audio <pe>... [--list]` | `AUDIO.XML` events with their `File`/`Volume`/`Buffers`, and how each `File` resolves under `AUDIO\SFX\` |
| `asf <pe>... [--list]` | Every ASF/WMA resource: codec, channels, sample rate, bits, bitrate, duration, packet count |
| `wma <pe>... [--all] [--resource NAME --out FILE.wav]` | Decodes WMA through Media Foundation; `--all` checks the decoded length against the container's declared duration |
| `png <pe> <name> [out.ppm]` | Decodes one PNG resource, optional PPM dump |
| `pngscan <pe>...` | Decodes every PNG resource and reports formats and failures |
| `rgmh <file>` | Reports the `RGMH` save-file header by offset, with only checkable claims |
| `rgmhrt <in> [out]` | Reads an `RGMH` file and re-emits it from the parsed fields, then compares byte for byte |
| `sha256 <file>` | Hex digest for cross-checking extraction |

## pple-xml

```
pple-xml <pe>... [--refs] [--examples N] [--path SUBSTR] [--list]
```

Reports parse coverage, root elements, the element-path histogram with each path's attributes
and example values, and — with `--refs` — how every resource reference resolves.

## pple-view

```
pple-view <pe>... [--menu] [--resource NAME] [--sprite XML] [--scene XML] [--movie XML]
                  [--movie-name NAME] [--anchor X,Y] [--frame N] [--state N]
                  [--pairs [D,L,S]] [--pairs-reveal] [--pairs-click I,J] [--pairs-ticks N]
                  [--pairs-solve] [--mce] [--no-container-offsets] [--mui PATH]
                  [--play NAME] [--volume N] [--mute]
                  [--script-click "x,y;x,y"] [--shot-delay MS] [--bench N] [--bench-build N]
                  [--list] [--shot out.ppm] [--save out.ppm] [--width N] [--height N]
                  [--font PATH] [--all]
```

`--menu` is the real main menu and the entry point: `PurblePlaceManager.xml` (which lives in
`PurblePlace.dll`) for the background, the `Foreground` sprite (which paints the four icons), the
title labels and the four `<NodeButton>` hot-spots, with each button's `<Animation>` 1 as the
hover art.  Clicking the Purble Pairs hot-spot enters the game and the in-game
`ReturnToMainMenu` button comes back; `ESC` goes back too, and `N`/`R` start a new round.
`--mce` additionally draws the Media Center build's `New Game` / `Exit Game` header buttons —
the shipped executable *deletes* those five nodes when `g_bMediaCenter == 0`
(`ConcentrationGame::LoadGameUI` `0x10008122B`), so they are absent by default, as they are in
this build.  `--no-container-offsets` turns off the engine's HUD re-parenting described below.

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
`--pairs-solve` clicks every matchable pair (equal `<id>`, or either card the Wild Card) and
`--pairs-ticks N` advances the engine's fixed 30 Hz step N times first, so the time bar, the red
threshold and the loss state are reachable immediately. `--bench N` renders N frames with vsync
off and reports the card-layer cost; `--bench-build N` reports the cost of N whole scene rebuilds.

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

## Verification

| Check | Result |
|---|---|
| SHA-256 self-test vs recorded hashes | `PurblePlace.exe`, `slc.dll` match |
| `verify` on the three asset DLLs | 85 CABs, 0 block mismatches, inflated totals equal summed `cbFile` |
| `pngscan` | **114 PNGs decoded, 0 failures** |
| `pple-xml` | **840 XML resources parsed, 0 failures**; 3425 distinct element paths |
| Re-extracted assets vs independent Python extraction | byte-identical |
| `pple-view --shot` | PNG, CAB→BMP and JPEG paths all render; sprite frames verified |
| `pple-view --scene` | `CONCENTRATIONGAME.XML` on its own renders 11 sprites and no labels, because `ConcentrationGame::LoadGameUI` deletes the five Media-Center nodes; with `--mce` it is 13 sprites + 3 labels, and `NewGame`/`ExitGame` measure 0% art (correctly transparent — see the note above). The untimed Beginner bar background is hidden |
| `pple-res split` | 11 split sheets, each with exactly four `x2` page resources (44 in `PurblePlace.dll`) |
| `pple-view --sprite --state` | `BUILDAPURBLEBUTTON.XML` state 0 transparent, state 1 light art, state 2 dark art, matching the running game's own menu screenshot |
| `pple-res rgmhrt` | All four `RGMH` files on disk (3 settings + 1 savegame, 83,826–87,542 bytes) re-emitted **byte-identical**; `windowprefs.xml` correctly rejected as not `RGMH` |
| `pple-view --movie` | `PRO_IDLE.XML` 61 frames / 31 parts, all drawn, frames 0/20/35/50 differ; `SMILEYBURSTMOVIE.XML` 88 frames / **322 parts**, all drawn |
| `pple-res pairs --autoplay` | Beginner 12/12 pairs score 240, Intermediate 18/18 score 360, Advanced 32/32 score 640, no mismatches — the deck, match rule and scoring agree end to end |
| `pple-view --pairs` | Advanced 8x8 draws **all 64 cells** from real card sprites; cell 49x49 at scale 40%, face-up 64 / backs 0 / sprite failures 0 |
| `pple-res muicheck` | Against `PurblePlace.dll` alone: **798 ids referenced, 796 (99.7%) byte-identical** to `en-US\PurblePlace.exe.mui`, 2 divergent, 0 absent. Adding `CardGames.dll` raises the total to 1,154 ids with 245 absent — all of them Solitaire ids that this MUI does not carry |
| `pple-view --mui` | Installs the MUI as the label localiser; the menu title's font changes `Comic Sans MS` → `Segoe UI` and `LOADING...` gains the MUI's trailing space |
| `--scene` label alignment | `<Justify>` decoded as a `DrawText` mask; the menu title now sits vertically centred in its box |
| `pple-res audio` | 150 manifest events (PurblePlace.dll 123, CardGames.dll 27), 147 resolve to a WMA resource, 3 unshipped |
| `pple-res asf` | **All 201 ASF/WMA resources parse**, every one WMA v2, 2ch 44.1 kHz 16-bit, with real durations |
| `pple-res wma --all` | All 201 decode through Media Foundation, 0 failures, 343.35 s total; decoded length agrees with the container's declared duration for **201 of 201** |
| `pple-view --play` | `UI_BUTTON` resolves through `AUDIO.XML` to `PURBLE_BUTTON_STEREO.WMA`, decodes to 0.325 s and plays through the SDL **wasapi** driver |
| `pple-view --pairs` sounds | `Con_TileFlip`/`Con_Correct`/`Con_TileFlip_Incorrect` load at the manifest's 30% volume; `--pairs-click 0,1` on seed 9 scores a match (20) and `--pairs-click 2,6` arms the reveal timer |
| `pple-view --menu` | runs `UI\PurblePlace\PurblePlaceManager.xml` from `PurblePlace.dll`: 8 items drawn, **4 hot-spots** at `509,145 292x250` (Build-a-Purble), `210,153 248x256` (Comfy Cakes), `2,82 212x213` (Purble Pairs), `2,373 93x100` (Exit). `menu-hotspots2.png` overlays the same rectangles on `PurblePlaceForeground.png` and every one lands on its painted icon |
| `pple-view --menu --script-click "330,280"` | enters the Comfy Cakes hot-spot: the menu rebuilds with that button on `<Animation>` 1 and the house gains its bright hover glow in the right place, the HUD names `ComfyCakesButton`, and the click reports the game as unimplemented |
| `pple-view --menu --script-click` | `100,180;200,60;760,470` logs `menu: ConcentrationButton -> Purble Pairs`, `click 0 -> revealed`, `pairs: ReturnToMainMenu -> main menu` — menu → game → flip → menu, in one run; the window stays up |
| `pple-view --pairs-solve` | a whole round through the viewer's own click handler on all three difficulties: Beginner **12/12 score 240**, Intermediate **18/18 score 360**, Advanced **32/32 score 640**, all `WON`, each then swapping to `ProfessorWinAnimation` — the same scores `pple-res pairs --autoplay` gives. The solver matches exact ids first and spends the Wild Cards last, because the Advanced deck has a single Time Bonus and a single Chef's cake and a greedy wild match strands them |
| `pple-view --pairs "Intermediate,0" --pairs-ticks N` | 3000 ticks → 110 s left, bar half drained; 5500 → 27 s left and the last segment is **red**; 6300 → `LOST`, `professor -> ProfessorLoseAnimation`, and the bar is empty. 226 segments, `TimeBarGreen`'s own `<FrameCount>` |
| `pple-view --pairs` card flip | `--script-click "100,180;200,60" --shot-delay 60` captures the back collapsed to ~1/3 width, `--shot-delay 130` the face expanding — the two phases of `Card::UpdateFlipForward` (`0x10007EA20`) |
| `pple-view --mce` | 13 sprites and 3 labels instead of 11 and 0; `New Game` and `Exit Game` labels appear in the 51 px header strip and the clicks log `pairs: NewGame -> new round` and `pairs: ExitGame -> main menu`. Without `--mce` the same clicks do nothing |
| `pple-view --bench-build 20 --pairs "Beginner,0"` | a whole scene rebuild with every sheet cached: **17.183 ms/call**. `--bench 300` with the reveal timer armed: the card layer, which is what replaced it, is **0.188 ms/call** over 84 calls — about 91x cheaper per 30 Hz tick |
| `pple-view --scene` container offsets | `TimeBarBkg` moves `3,21` → `63,4`, the jar to `15,315`, the peek background and its tokens to `0,398`, the Professor's movie anchor to `295,70`; `CONCENTRATIONGAME.XML` renders as the shipped screen (`new-conc.png` next to the game's own 256x192 thumbnail) |
| `pple-view --scene` power-of-two padding | `<PlayArea>`'s `148,0 800x500` now lands on a 1024x512 texture, so the 800x496 PNG is not stretched; the blue board's right edge lands at screen x **588.9**, which is where the game's own screenshot puts it (589). The unpadded reading gives 661.6 |

Viewable items: `PurblePlace.dll` 211 (67 PNG + 71 CAB + 73 JPEG), `PurblePlace2.dll` 24,
`CardGames.dll` 65 (27 + 14 + 24).

## Known gaps

* The exact on-disk geometry of a split sheet's **fourth** `x2` page (SHEET18's is 355x23 where
  the other three are ~1024x1024). The page path is only reachable with `g_bDoubleDPI` set, and
  `Material::Render` (`0x1000A96E0`) rescales each page by `1024 / pageDim`, so the engine is
  expected to absorb it. Not derived; not needed for the normal-DPI path.
* Movies are evaluated but not free-running: `--movie --frame N` is a still, and the library's
  `fps` is reported rather than played. The **Professor** is the exception because the game
  screen needs it: the idle/win/lose movie is loaded once and advanced at `movie_frame_rate`
  (`Professor::InitUI` `0x10008BD94` anchors it at `/ProfessorContainer` `295,70`). Rotation
  pivots about each part's destination centre; the engine's pivot is not established.
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
  match rule, scoring, placement, container parenting and flip animation are reversed; see
  sections 14 and 17 of `D:\Development\re\notes\purbleplace.md`. Not implemented:
  * the special cards beyond the Wild Card — the Mixer, the Chef's cake, the Time Bonus and the
    sneak peek all currently behave as ordinary pairs. The Wild Card **is** implemented
    (`Card::DoMatcher` `0x10007D224`: equal `<id>`, or either id is `Board+0x180`), and
    `--pairs-solve` exercises it;
  * the `x3`/`x2`/`x1` multiplier that `Board::MatchMade` applies, whose input is read from a
    stack slot with no corresponding argument (the reconstruction uses the `x2` branch);
  * `<NumCardTypes>`, which is read from the XML into `Level+8` and has no reader I could find;
  * the **progress meter**: `ProgressMeter::InitUI` (`0x100084720`) builds four indicator sprites
    from `*(Board+0x20) + 8*i` at `+0x508` (1,288) and stacks them `55` px apart, lighting one
    per matched pair (`Board+0x124`). Those per-level indicator sprite names are not produced by
    `Difficulty::Init` (`0x10008BA7C`), which only reads `<TotalTime>`, `<NumCardTypes>` and
    `<CardPairs>` per level, so the reconstruction does not know the sprite names and draws no
    meter. The jar, the sneak-peek tokens and the Professor are drawn;
  * the **sneak peek** tokens are drawn at the right place out of `NumStartTokens`' three token
    slots, but they are inert: `Board::SneakPeaker` / `SneakPeakCheck` / `SelectSneakCard` are not
    reversed;
  * `<NumStartTokens>` is parsed and used for the token count only.
* The engine's own HUD text on an untimed round has no time bar, which the reconstruction
  reproduces (`TimeBar::InitUI` `0x10008538E` hides `/TimeBarBkg` when the difficulty is 0).
* 240 distinct references (464 occurrences) do not resolve — content variants that were never
  shipped and other games' backgrounds. The game's own loader tolerates misses.
