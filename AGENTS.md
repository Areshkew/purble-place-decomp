# Purble Place reconstruction — instructions for every round

## Scope and working rules

- Reconstruct the **main menu and Purble Pairs only**. Do not start Comfy Cakes or Purble Shop unless the user changes the scope.
- Extend the existing reconstruction; do not rewrite completed systems or repeat settled analysis.
- Keep answers concise. Distinguish observed behavior, binary evidence, implementation approximations and hypotheses.
- Stay offline for game binaries and assets. Do not modify anything under `C:\Program Files`.
- Do not defeat, patch or bypass licensing, activation or edition checks. Static `slc.dll` analysis is allowed; modifying it is not.
- Work in small coherent milestones. Validate each one, update the record, then request a focused local commit through DSH's approval prompt. Stage only that milestone's files. Do not push automatically.
- Preserve unrelated or preexisting changes. In particular, `HANDOFF-session6.md` was an existing untracked user file at the end of session 6; do not stage or overwrite it incidentally.

## Read first, then check state

1. `D:\Development\re\RE-SETUP.md`: authority for tool locations, machine rules and RE workflow.
2. `D:\Development\re\notes\purbleplace.md`: read the **highest-numbered section first** — currently section 28 (the game's own double-DPI `x2` art); section 20 holds the jar/sneak fixes and corrections, section 19 the previous completed work, and sections 10–27 remain useful where later sections do not supersede them.
3. `tools\pple-res\README.md`: how to run, measured verification results and honest known gaps.
4. Run `pwd`, `git status --short` and `git log -5 --oneline`. Do not infer the current directory from the DSH installation path.
5. Read relevant source before editing it. Use DSH `read`, `glob` and `grep` for text inspection, discovery and content searches.
6. For viewer changes, start with `tools\pple-res\src\viewer\README.md` (module map), then read only the owning module. `view_main.cpp` is now an 8-line entry point; do not reconstruct the old monolithic viewer or re-read the full historical notes for a local change.

## Locations and tools

| Purpose | Location |
|---|---|
| Workspace | `D:\Development\purble palace` |
| C++ project | `tools\pple-res` |
| Third-party SDL development trees | `tools\third_party` — NOT `tools\pple-res\third_party` |
| Build executables | `build\pple-res` — directly in this directory |
| Game working copy | `C:\re\samples\purbleplace` — analyze/debug only this copy |
| Localized strings | `C:\re\samples\purbleplace\en-US\PurblePlace.exe.mui` |
| Captures, extracted data and test logs | `C:\re\traces\cpp` |

- Toolchain: MinGW-w64 g++ 15.2.0, CMake 4.2.3 and Ninja on PATH.
- Static analysis: IDA MCP, session `purble`, input `C:\re\samples\purbleplace\PurblePlace.exe`. Workers can expire despite a requested idle TTL. Reopen with `idb_open` and the same `preferred_session_id`; saved annotations persist.
- Start binary analysis with `survey_binary` after opening the database. Decompile **one function at a time**; never request a whole-binary decompilation.
- Dynamic analysis: x64dbg MCP at localhost port 9094, only while x64dbg is running. **Call GetDebugState first.** Inspect memory only while paused. After `run`, immediately call WaitForEvent or WaitForPause. Prefer attaching to an already-running game to avoid startup/TLS stepping.
- Original game can jump to Pairs with command-line argument `Concentration_`; launch with its working directory set to the sample directory.
- x64dbg `poi(...)` can incorrectly return zero for valid pointers here; use ReadMemory for pointers. The game's debugger database may already break at Board::Update (`0x10008A4F0`).
- Use `load_workspace_dependencies` for the bundled Python path. Pillow is in DSH's runtime Python, not the RE venv. Pillow opens PPM directly; no conversion dependency needs installation.

## Build and launch

```powershell
cmake -S tools/pple-res -B build/pple-res -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/pple-res

$d = 'C:\re\samples\purbleplace'
$m = "$d\en-US\PurblePlace.exe.mui"
$game = @("$d\PurblePlace.dll", "$d\PurblePlace2.dll", "$d\CardGames.dll")

& "$PWD\build\pple-res\pple-view.exe" @game --menu --mui $m
& "$PWD\build\pple-res\pple-view.exe" @game --menu --loading 1200 --mui $m
& "$PWD\build\pple-res\pple-view.exe" @game --scene 'UI\CONCENTRATION\CONCENTRATIONGAME.XML' --pairs 'Beginner,0,0' --mui $m
```

Use the absolute `$PWD` invocation if PowerShell does not recognize a bare relative executable path. Check nonzero exits before proceeding. Track background job IDs; collect relevant results before finishing.

## Resource and evidence traps

- `CONCENTRATIONGAME.XML`, `CARDS.XML`, Loading and manager XMLs exist in `PurblePlace.dll`. Other `UI\Concentration\*.xml` resources are in **PurblePlace2.dll**. Search both when resolving game data.
- `pple-res list --grep` filters before case folding and can miss names. Use `pple-res find <pe>... <payload text>` instead.
- IDA renders some field accesses as `add rdx, 408h`, not `+408h`. Prefer `insn_query` with the **decimal immediate** for field-access searches. Bracketed `mov`/`cmp` accesses can be searched as listing text.
- Never convert number bases or bit masks by hand; use IDA `int_convert` or Python.
- Recovered function names and comments are not authoritative symbols from the image. Cite **addresses and offsets**, tool-output lines or observed breakpoints for binary claims.
- The shipped 256x192 thumbnail is a static visual reference, not evidence of animation timing or z-order. Label comparisons accordingly.

## Settled behavior — do not regress or re-derive from older notes

- Menu, drifting clouds, loading screen, all three Pairs difficulties and their multi-board rounds are implemented.
- Card flips, four special cards, match sparks/blank tiles, progress meter, audio map, jar tray/arcs/burst, rank scoring and settings roundtrip are implemented.
- **Jar order:** cards/sparks, then JarBase/JarBkg, burst movie, arc coins, tray coins, JarFg. Cached static draws must remain intact across refreshes; truncating an already reordered vector corrupts subsequent frames.
- **Sneak duration:** raw `/SneakRevealDuration` = **45 ticks**, armed after the flip. `/SneakPeakIconDur` controls the mini-icon schedule, NOT the reveal. The model's flip delay is 10 ticks; do not present that as a newly observed original-game duration.
- **Sneak cost:** float32 `/SneakTimeDecrement` percent of the level budget, truncated and clamped to zero when timed: 315 ticks on Intermediate, 270 on Advanced; Beginner costs zero.
- **Sneak tokens:** per-entry x/y/scale, not a stack. Nodes are (0,47), (44,47), (90,47) within container (0,398); registration-adjusted rects are (3,448), (47,448), (93,448), 45x46 at default scale 100%. Draw/click index i only while i+1 <= token count. Beginner starts with 2, Intermediate 1, Advanced 0.
- **Sneak movie:** 26 frames, 15 fps, 23 parts. Its Base is parented to the **Board Base**, not the token container: anchor **(-18,-70)**, NOT (-18,328). Evidence: creation `0x100084B20`, position `0x100084B35`, caller `0x1000892DB`. Cache the movie across rebuilds; reset/show on use and hide when complete.
- **Mismatch:** `/RevealDuration` = 8 raw ticks after the reveal callback, not seconds.
- **Viewport:** Pairs design bounds are 800x496, not the accumulated extents of animated parts. Offscreen effects must not progressively shrink the scene.
- **Capture sizing:** diagnostic HUD height depends on text/font. On this setup it is **80 px** for Pairs: `--width 800 --height 576`, crop `(0,80,800,576)` for 1:1. The older 556-height/60-pixel claim is incorrect here.
- **Solve hooks:** `--pairs-solve` and `--pairs-click` skip jar animation. Use **normal `--script-click`** for flight/burst captures; do not fabricate burst output for a solve run.
- **Double-DPI art:** `--hidpi` / `--no-hidpi` / automatic above a 1.5x window scale (back to 1x below 1.425x) draw every sheet from the engine's `x2` resources: a `SPLITLIST.XML` sheet becomes its four `<stem>x2<page>` pages assembled into a twice-size canvas, anything else becomes its `"x2"`-suffixed sibling (`ResourceTextureD3D::SetNameForDPI` `0x1000A80DC`, `GetSplitTexture` `0x1000A7E40`, `Recreate` `0x1000A7FEC`). A page is a top-left-aligned crop of its quadrant at 2x, so `pot(2w) = 2*pot(w)` holds and a source rect authored in 1x space is simply read at twice its offset and size; the layout, hot-spots, hit tests and destination rects must stay identical between the modes. Do not add a per-resize reload to any per-tick path.
- Matches may exceed the pair total because completion force-matches leftover Wild Cards; Beginner 13/12 is intentional. Scoring uses constant x3 and 1/2/4 boards.
- Media Center's 51-pixel host offset remains a **hypothesis**. The remaining spark counter's interpretation as fade is an approximation.

## Verification before claiming success

Run relevant checks before and after each milestone. For the full current matrix, inspect and run:

- `C:\re\traces\cpp\s27-verify.ps1`: the current matrix (renamed from `s7-verify.ps1`), 29 command runs, including assets, audio, rules, menu/navigation/loading, solves/loss, scene/MCE, match/mismatch, benchmarks and trace-directory save roundtrip.
- `C:\re\traces\cpp\s27-assert-verify.py`: **32 command logs, 66 measured-output assertions**, all PASS. These external scripts are not tracked in this repository; read them before relying on them. Its fixed 34-card-refresh benchmark assertion is wall-clock-dependent (the unchanged session-9 baseline measured 33); require a positive refresh count plus one full build instead.
- `C:\re\traces\cpp\s28-run.ps1 -Prefix P -Extra FLAG -BuildDir D` and `s28-verify-all.ps1` run the whole matrix with `--hidpi`/`--no-hidpi` forced into **every viewer command** (the flag is injected inside `Check`, not into each argument list), and `s28-compare.py`, `s28-asserts.py`, `s28-compare-shots.py`, `s28-switch-check.ps1` and `s28-perf.ps1` check it: the 1x path against a pre-change build (`git stash` + `cmake -S tools/pple-res -B build/pple-before`), the 2x path for identical structure and sharper pixels, both switch directions, and the cost. See notes 28.6.
- `pple-res pairs <PurblePlace2.dll> --difficulty D --sneak-check`: one-token fixture, including Advanced, checks duration, cost, repeated use, expiry and zero-clamp/loss. It does not imply Advanced starts with a token.
- Deterministic level-change and normal-click burst commands/captures are recorded in section 20 and the `s7-*.log` trace files.

Expected baseline:

| Check | Expected |
|---|---|
| CAB verify | 71 + 14 CABs, zero failed and zero block mismatches |
| PNG scan | 67 / 20 / 27 decoded, zero failures |
| XML inventory | 840 parsed; 3652 references seen, 3047 resolved |
| WMA decode | 201 decoded, zero failures; 201/201 duration agreement |
| UI_BUTTON playback | 0.325 s, wasapi |
| CLI autoplay score / rank total | Beginner 390 / 2690; Intermediate 1120 / 7120; Advanced 3920 / 13920; rank 5 (Beginner was 410 / 2710 while `Board::ShuffleCards` still pinned the odd board's Wild Card to the last cell) |
| Viewer solve score / rank total | 410 / 2710; 1180 / 7180; 4040 / 14040; rank 5 |
| Settings `consave` | XML and file both BYTE-IDENTICAL |
| Scene-only / MCE | 14 sprites, 0 labels / 16 sprites, 3 labels |
| Beginner playable scene | 6 static sprites (supersedes 8 after hiding the unused sneak animation template and initial sparks); token draws are separate and count-aware |
| Hover | the engine has no separate hover: `UserInterface::ProcessMouseMove` (`0x100098498`) hit-tests the node under the cursor and `SetFocus` (`0x1000992E0`) sends `FocusGot`/`FocusLost`, so one node at a time is focused. A card shows `highlite_outline` (`Card+0xD0`) and, face up, the additive `card_glow` (`Card+0xC8`); a `NodeButton` (menu buildings, `ReturnToMainMenu`, sneak tokens) switches to `<Animation>` 1. The outline is the focus art, **not** a "waiting for its partner" marker. Motion is routed on every screen; `move:x,y` scripts one |
| Warm performance, session 6 | full build 17.899 ms/call; card refresh 0.0835 ms/call, first 9.2018 ms |
| Double-DPI mode line | `hidpi: double-DPI (x2) art, window scale S, threshold 1.50` at 1400x900 and 800x556 both forced; `--hidpi` reports `212 sheet(s) drawn from the x2 resources, 0 fallback(s)` over the 21-command matrix, `--no-hidpi` reports `0` |
| Double-DPI cost (session 28) | cached full build 17.842 -> 32.158 ms/call; primed card layer 0.0801 -> 0.0863 ms/call; peak working set 110.8 -> 151.0 MB (menu) and 117.7 -> 168.4 MB (Beginner); one frame 875 -> 883 ms and 820 -> 1101 ms |
| Double-DPI sharpness (session 28) | edge energy on the deterministic stills x1.14–x1.28 (x0.98 for the flat `loading` photograph); worst 32x32 block difference between 1x and 2x 28/255 |

Do not reintroduce a per-tick full scene rebuild. Keep the test hooks (`--script-click`, `--pairs-solve`, `--pairs-ticks`, `--bench*`, `--shot-delay`) usable. At 5500 ticks the code selects the resource named RedTimerBar; current captured pixels look gold/yellow, so do not claim a verified red-pixel change. At 6300 ticks Intermediate loses and the bar is empty.

## Next work and record maintenance

Remaining gaps: the **loading screen on a transition** (the shipped build calls `Engine_SetBackMode(1, Loading.xml)` from `ConcentrationGame::LoadGameUI` `0x100081008`, while `start_game`/`game_to_menu` still jump straight between screens); Professor tips/transitions and persisted tip settings integration; remaining card/board effects; resumable SaveGameState's own file name and schema; small unknowns. The double-DPI `x2` art is implemented for the menu, the loading screen, the chooser and Pairs (session 28). The behavior-preserving viewer module split is complete; module ownership and navigation are documented in `tools\pple-res\src\viewer\README.md`. See the README and section 20 before choosing a milestone.

For each new result, update `D:\Development\re\notes\purbleplace.md` and the project README. Correct superseded claims explicitly rather than silently leaving contradictions. Add captures under `C:\re\traces\cpp`, and report measured numbers, not assumed old values. Notes live outside this project's Git repository and must be updated separately.

Last completed reconstruction milestones: `c372a39` (jar layering), `b714597` (sneak timing/tokens/movie, stable viewport, regression hook and README). Session 6 evidence and final comparisons use the `s7-` trace prefix; earlier exploratory captures under that prefix are labeled as such in section 20.
