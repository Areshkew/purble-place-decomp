<#
  Builds the browser viewer: build\web\index.html + pple-view-web.js/.wasm/.data.

    .\build-wasm.ps1                       # uses D:\Development\emsdk and build\web
    .\build-wasm.ps1 -Emsdk C:\emsdk -BuildDir D:\out
    .\build-wasm.ps1 -CopyAssetsFrom 'C:\re\samples\purbleplace'
                                           # also fills build\web\game for ?auto=1 runs;
                                           # game files are never committed (see README.md)

  Emscripten is the whole toolchain: SDL3, SDL3_ttf and libjpeg come from its own ports.  The
  script downloads nothing; the first build lets Emscripten fetch and compile those ports.
#>
[CmdletBinding()]
param(
    [string]$Emsdk = $(if ($env:EMSDK) { $env:EMSDK } else { 'D:\Development\emsdk' }),
    [string]$BuildDir = '',
    [string]$CopyAssetsFrom = ''
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$src = Join-Path $repo 'tools\pple-res'
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\web' }

function Write-Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }

# --- the toolchain -------------------------------------------------------------------------
$emcc = Join-Path $Emsdk 'upstream\emscripten\emcc.exe'
$emcmake = Join-Path $Emsdk 'upstream\emscripten\emcmake.exe'
foreach ($tool in $emcc, $emcmake) {
    if (-not (Test-Path $tool)) {
        throw "$tool is missing. Install the SDK first:`n" +
              "  git clone https://github.com/emscripten-core/emsdk `"$Emsdk`"`n" +
              "  cd `"$Emsdk`"; .\emsdk.bat install latest; .\emsdk.bat activate latest"
    }
}
$env:EMSDK = $Emsdk
$env:EM_CONFIG = Join-Path $Emsdk '.emscripten'
$env:EMSDK_NODE = Join-Path $Emsdk 'node\24.19.0_64bit\node.exe'
$env:EMSDK_PYTHON = Join-Path $Emsdk 'python\3.13.3_64bit\python.exe'
$env:PATH = "$Emsdk;$Emsdk\upstream\emscripten;$env:PATH"
Write-Step "emsdk at $Emsdk"
& $emcc --version | Select-Object -First 1

# --- the viewer ----------------------------------------------------------------------------
Write-Step "configuring pple-view-web into $BuildDir"
& $emcmake cmake -S $src -B $BuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_TESTING=OFF `
    -DPPLE_BUILD_VIEW=ON
if ($LASTEXITCODE -ne 0) { throw 'cmake configuration failed' }

Write-Step 'building pple-view-web'
cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw 'the web build failed' }

# The page is the shell this build was configured with; serve it as index.html.
$html = Join-Path $BuildDir 'pple-view-web.html'
if (Test-Path $html) { Copy-Item $html (Join-Path $BuildDir 'index.html') -Force }

# --- optional: a served deployment, for the ?auto=1 path (never committed) -----------------
if ($CopyAssetsFrom) {
    Write-Step "copying game files from $CopyAssetsFrom into $BuildDir\game (not part of the repo)"
    $game = Join-Path $BuildDir 'game'
    New-Item -ItemType Directory -Force -Path $game, "$game\en-US" | Out-Null
    foreach ($name in 'PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll') {
        Copy-Item (Join-Path $CopyAssetsFrom $name) $game -Force
    }
    $mui = Join-Path $CopyAssetsFrom 'en-US\PurblePlace.exe.mui'
    if (Test-Path $mui) { Copy-Item $mui "$game\en-US" -Force }

    # The sound pack: the shipped effects are WMA, which no browser decodes, so the desktop tool
    # (the one build that has Media Foundation) writes them out as WAVs for the page to play.
    $nativeRes = Join-Path $repo 'build\pple-res\pple-res.exe'
    if (Test-Path $nativeRes) {
        Write-Step 'writing the sound pack (WMA -> WAV) with the desktop tool'
        & $nativeRes audio-pack (Join-Path $game 'PurblePlace.dll') (Join-Path $game 'PurblePlace2.dll') `
            (Join-Path $game 'CardGames.dll') --out (Join-Path $game 'audio')
        if ($LASTEXITCODE -ne 0) { Write-Warning "audio-pack exited $LASTEXITCODE" }
    } else {
        Write-Warning "no desktop pple-res.exe at ${nativeRes}: the served copy will be silent"
    }
}

Write-Step 'done'
Get-ChildItem $BuildDir -File | Select-Object Name, @{ n = 'MB'; e = { [math]::Round($_.Length / 1MB, 2) } } |
    Format-Table -AutoSize
Write-Host "Serve it from $BuildDir, for example:" -ForegroundColor Cyan
Write-Host "  python -m http.server 8080 --directory `"$BuildDir`""
Write-Host "  then open http://127.0.0.1:8080/"
