# Purble Place reconstruction

A C++20 reconstruction of the Purble Place mini-games, written from static analysis of the
shipped binaries: the main menu, Purble Pairs, Comfy Cakes and Purble Shop. The viewer is SDL3;
the resource, XML, rules and animation code in `tools/pple-res/src` is standard-library only.

None of the original game's code or art is in this repository. The tools read the game's own
resource DLLs at run time, and the game files are never copied into the tree.

## Play in a browser

**<https://areshkew.github.io/purble-place-decomp/>**

That page is the WebAssembly build of the viewer, rebuilt and published from `main` by
[`.github/workflows/pages.yml`](.github/workflows/pages.yml). Because the site cannot ship
Microsoft's files, it asks for them instead: pick `PurblePlace.dll`, `PurblePlace2.dll`,
`CardGames.dll` and `en-US/PurblePlace.exe.mui` from your own installation. They are read in the
browser and are not uploaded.

Sound is optional and needs one extra step. The shipped effects are WMA, which no browser
decodes, so the desktop build writes a WAV pack once:

```powershell
pple-res audio-pack PurblePlace.dll PurblePlace2.dll CardGames.dll --out audio
```

Then choose that folder in the page's **Sound** section. Without it the game is silent.

## Build

```powershell
cmake -S tools/pple-res -B build/pple-res -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/pple-res
```

The browser build needs Emscripten; see [tools/pple-res/web/README.md](tools/pple-res/web/README.md).

## Documentation

| Document | What it covers |
|---|---|
| [tools/pple-res/README.md](tools/pple-res/README.md) | How to run the viewer and the command-line tools, the resource-resolution rules as implemented, and what each check measures |
| [tools/pple-res/web/README.md](tools/pple-res/web/README.md) | The browser page: module map, the published site, and the two headless checks |
| [tools/pple-res/src/viewer/README.md](tools/pple-res/src/viewer/README.md) | Viewer module map: what owns each screen, its state and its clocks |
| [AGENTS.md](AGENTS.md) | The current scope, the settled behaviour not to regress, and the expected verification baseline |
