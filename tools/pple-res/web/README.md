# Browser viewer module map

- [shell.html](shell.html): local-file picker, bounded sound-pack loading (six requests),
  status/errors, canvas fitting, Back / exit, full screen, help, collapsed diagnostics.
  Owns page state; C++ owns the running game and signals session teardown.
- [build-wasm.ps1](build-wasm.ps1): offline-installed emsdk build and optional asset staging;
  writes the generated page to `build/web/index.html`. Game data is never committed.
- [smoke-browser.mjs](smoke-browser.mjs): headless Edge/CDP navigation, viewport checks,
  screenshots and CPU profiles. `--scene-click X,Y` converts game coordinates to real
  browser mouse events; `--ui-click ID` activates a page control with an explicit CDP
  user gesture (stable across headless full-screen/focus changes); `--key Escape` sends a key.
- [smoke-node.mjs](smoke-node.mjs): seven local-asset wasm data checks, no renderer.
- [fonts](fonts/): preloaded Noto Sans and its license.
- [.github/workflows/pages.yml](../../../.github/workflows/pages.yml): the published page. Builds
  the same target with Emscripten and deploys it to GitHub Pages.

## The published page

<https://areshkew.github.io/purble-place-decomp/> is this build, rebuilt and published by
[pages.yml](../../../.github/workflows/pages.yml) on every push to `main` (and on
`workflow_dispatch`). GitHub Pages has no game files, so the published page is the file-picker
path: the player selects their own `PurblePlace.dll`, `PurblePlace2.dll`, `CardGames.dll` and
`.mui`, and those files stay in the browser.

The workflow runs the same CMake commands as [build-wasm.ps1](build-wasm.ps1), with the
Emscripten version it pins as `EM_VERSION`, and then copies `pple-view-web.html` to `index.html`
with the `.js`/`.wasm`/`.data` beside it. Nothing else is published: no game files, no sound
pack, no `game/` folder. To stage the same four files locally:

```powershell
.\tools\pple-res\web\build-wasm.ps1          # writes build\web
python -m http.server 8099 --directory build\web   # same page, same relative URLs
```

The generated page loads `pple-view-web.js` from a relative URL and lets Emscripten resolve the
`.wasm` and `.data` beside it, so serving from a sub-path such as `/purble-place-decomp/` works
unchanged. `?auto=1&assets=` remains a local/served convenience for the headless checks; Pages
never uses it, because it would mean publishing the game's files.

## Normal play and diagnostics

The game has no diagnostic strip during normal play. The page fits the complete
canvas to both the available width and height without resizing/reloading game textures.
Logs stay in the closed Diagnostics panel (last 300 lines, batched per event-loop
turn so final messages remain visible after rendering stops). `?debug=1` opens the panel and enables the diagnostic strip.

`?auto=1&assets=game/` uses served local assets; `?shot=1` preserves the original
one-frame capture hook, including its diagnostic strip and 1400x900 canvas.
No game files are uploaded. Save state still lives only in page memory.

```powershell
.\tools\pple-res\web\build-wasm.ps1
node tools/pple-res/web/smoke-node.mjs build/web C:/re/samples/purbleplace
node tools/pple-res/web/smoke-browser.mjs --url 'http://127.0.0.1:8099/?auto=1&assets=game/' --out shot.png --check-ui --profile perf.json --measure 5000 --check-perf
```

The performance guard detects SDL's clock-heavy simulated-VSync fallback. Real
interactive FPS also depends on the browser/GPU; the headless check disables the GPU.
The build must be rebuilt and the served page reloaded after source changes.
