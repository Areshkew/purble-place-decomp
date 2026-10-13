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
