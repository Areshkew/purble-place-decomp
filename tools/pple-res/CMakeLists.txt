cmake_minimum_required(VERSION 3.20)
project(pple-res LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Core: PE resources, CAB/MSZIP, DEFLATE, PNG, SHA-256.  Standard library only.
# ---------------------------------------------------------------------------
add_library(pple_core STATIC
  src/pe_image.cpp
  src/cab.cpp
  src/inflate.cpp
  src/png.cpp
  src/png_write.cpp
  src/xml.cpp
  src/scene.cpp
  src/movie.cpp
  src/concentration.cpp
  src/cakes.cpp
  src/shop.cpp
  src/animation.cpp
  src/mui.cpp
  src/split_list.cpp
  src/rgmh.cpp
  src/sha256.cpp
)
target_include_directories(pple_core PUBLIC include)

include(CTest)
if(BUILD_TESTING)
  add_executable(pple-concentration-test tests/concentration_test.cpp)
  target_link_libraries(pple-concentration-test PRIVATE pple_core)
  add_test(NAME concentration-eye-reward COMMAND pple-concentration-test)
  add_executable(pple-cakes-test tests/cakes_test.cpp)
  target_link_libraries(pple-cakes-test PRIVATE pple_core)
  add_test(NAME cakes-model-and-widget COMMAND pple-cakes-test)
  add_executable(pple-cakes-clock-test tests/cakes_clock_test.cpp)
  target_link_libraries(pple-cakes-clock-test PRIVATE pple_core)
  add_test(NAME cakes-conveyor-clock COMMAND pple-cakes-clock-test)
  add_executable(pple-animation-test tests/animation_test.cpp)
  target_link_libraries(pple-animation-test PRIVATE pple_core)
  add_test(NAME resource-animation-timeline COMMAND pple-animation-test)
  add_executable(pple-shop-test tests/shop_test.cpp)
  target_link_libraries(pple-shop-test PRIVATE pple_core)
  add_test(NAME shop-rules COMMAND pple-shop-test)
endif()

if(MSVC)
  target_compile_options(pple_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(pple_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Audio: WMA decoding through Media Foundation.  Kept out of pple_core so that
# library stays dependency-free.  The WAV writer is portable and lives in
# wav.cpp; wma.cpp is the Windows decoder, wma_stub.cpp the web build's silent
# stand-in, so every target links the same interface either way.
# ---------------------------------------------------------------------------
set(PPLE_AUDIO_TARGET "")
if(WIN32)
  add_library(pple_audio STATIC src/wma.cpp src/wav.cpp)
  target_include_directories(pple_audio PUBLIC include)
  target_link_libraries(pple_audio PUBLIC mfplat mfuuid mfreadwrite ole32)
  set(PPLE_AUDIO_TARGET pple_audio)
  message(STATUS "pple_audio: enabled (Media Foundation)")
else()
  add_library(pple_audio STATIC src/wma_stub.cpp src/wav.cpp)
  target_include_directories(pple_audio PUBLIC include)
  set(PPLE_AUDIO_TARGET pple_audio)
  message(STATUS "pple_audio: built without Media Foundation, WMA decoding reports unavailable")
endif()
if(PPLE_AUDIO_TARGET AND NOT MSVC)
  target_compile_options(pple_audio PRIVATE -Wall -Wextra -Wpedantic)
elseif(PPLE_AUDIO_TARGET)
  target_compile_options(pple_audio PRIVATE /W4 /permissive-)
endif()
# ---------------------------------------------------------------------------
# CLI: pple-res
# ---------------------------------------------------------------------------
add_executable(pple-res src/main.cpp)
target_link_libraries(pple-res PRIVATE pple_core ${PPLE_AUDIO_TARGET})

# ---------------------------------------------------------------------------
# XML data-model inventory: pple-xml
# ---------------------------------------------------------------------------
add_executable(pple-xml src/schema_main.cpp)
target_link_libraries(pple-xml PRIVATE pple_core)

foreach(tool pple-res pple-xml)
  if(MSVC)
    target_compile_options(${tool} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${tool} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()

# ---------------------------------------------------------------------------
# Viewer: pple-view (SDL3).  Skipped automatically when SDL3 is not present.
# ---------------------------------------------------------------------------
# third_party/ sits next to this project, not inside it.
set(PPLE_SDL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL3-3.4.10/x86_64-w64-mingw32"
    CACHE PATH "SDL3 development tree (contains include/SDL3, lib, bin)")
set(PPLE_SDL_IMAGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL3_image-3.4.6/x86_64-w64-mingw32"
    CACHE PATH "SDL3_image development tree")
set(PPLE_SDL_TTF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL3_ttf-3.2.2/x86_64-w64-mingw32"
    CACHE PATH "SDL3_ttf development tree")
option(PPLE_BUILD_VIEW "Build the SDL3 resource viewer" ON)

# One source list, two targets: the desktop viewer below and the browser build at the end of
# this file.  They share every module; only the platform libraries differ.
set(PPLE_VIEW_SOURCES
  src/view_main.cpp
  src/viewer/app.cpp
  src/viewer/audio.cpp
  src/viewer/browser.cpp
  src/viewer/cakes_screen.cpp
  src/viewer/shop_screen.cpp
  src/viewer/cakes_chef.cpp
  src/viewer/cakes_tools.cpp
  src/viewer/cakes_transport.cpp
  src/viewer/cakes_clock.cpp
  src/viewer/cakes_play.cpp
  src/viewer/card_assets.cpp
  src/viewer/hud.cpp
  src/viewer/input.cpp
  src/viewer/level_art.cpp
  src/viewer/loop.cpp
  src/viewer/menu.cpp
  src/viewer/movie.cpp
  src/viewer/pair_hud.cpp
  src/viewer/pairs_effects.cpp
  src/viewer/pairs_render.cpp
  src/viewer/pairs_round.cpp
  src/viewer/pairs_setup.cpp
  src/viewer/pairs_state.cpp
  src/viewer/professor.cpp
  src/viewer/resources.cpp
  src/viewer/scene.cpp
  src/viewer/textures.cpp
  src/viewer/tips.cpp
  src/viewer/overlays.cpp
)

if(PPLE_BUILD_VIEW AND NOT EMSCRIPTEN)
  if(EXISTS "${PPLE_SDL_ROOT}/include/SDL3/SDL.h")
    add_executable(pple-view ${PPLE_VIEW_SOURCES})
    target_link_libraries(pple-view PRIVATE pple_core ${PPLE_AUDIO_TARGET})
    target_include_directories(pple-view PRIVATE "${PPLE_SDL_ROOT}/include")
    target_link_directories(pple-view PRIVATE "${PPLE_SDL_ROOT}/lib")
    target_link_libraries(pple-view PRIVATE SDL3)

    # Optional extras: JPEG decoding and the HUD font.
    if(EXISTS "${PPLE_SDL_IMAGE_ROOT}/include/SDL3_image/SDL_image.h")
      target_include_directories(pple-view PRIVATE "${PPLE_SDL_IMAGE_ROOT}/include")
      target_link_directories(pple-view PRIVATE "${PPLE_SDL_IMAGE_ROOT}/lib")
      target_link_libraries(pple-view PRIVATE SDL3_image)
      target_compile_definitions(pple-view PRIVATE PPLE_HAVE_SDL_IMAGE=1)
    endif()
    if(EXISTS "${PPLE_SDL_TTF_ROOT}/include/SDL3_ttf/SDL_ttf.h")
      target_include_directories(pple-view PRIVATE "${PPLE_SDL_TTF_ROOT}/include")
      target_link_directories(pple-view PRIVATE "${PPLE_SDL_TTF_ROOT}/lib")
      target_link_libraries(pple-view PRIVATE SDL3_ttf)
      target_compile_definitions(pple-view PRIVATE PPLE_HAVE_SDL_TTF=1)
    endif()

    if(MSVC)
      target_compile_options(pple-view PRIVATE /W4 /permissive-)
    else()
      target_compile_options(pple-view PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # Ship the matching runtimes next to the executable.
    foreach(runtime "${PPLE_SDL_ROOT}/bin/SDL3.dll"
                    "${PPLE_SDL_IMAGE_ROOT}/bin/SDL3_image.dll"
                    "${PPLE_SDL_TTF_ROOT}/bin/SDL3_ttf.dll")
      if(EXISTS "${runtime}")
        add_custom_command(TARGET pple-view POST_BUILD
          COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${runtime}"
                  "$<TARGET_FILE_DIR:pple-view>"
          VERBATIM)
      endif()
    endforeach()
    message(STATUS "pple-view: enabled, SDL3 at ${PPLE_SDL_ROOT}")
  else()
    message(STATUS "pple-view: disabled, no SDL3 headers under ${PPLE_SDL_ROOT}")
  endif()
endif()

# ---------------------------------------------------------------------------
# Viewer for the browser: pple-view-web (Emscripten).
#
# SDL3, SDL3_ttf and libjpeg are all Emscripten ports (-sUSE_SDL=3 /
# -sUSE_SDL_TTF=3 / -sUSE_LIBJPEG=1; the first two pull in freetype and
# harfbuzz).  SDL3_image has no port, so the web build takes the JPEG path in
# viewer/jpeg.cpp instead of IMG_Load_IO and never links SDL3_image.
#
# The game's own resource DLLs are deliberately NOT part of this build: the page
# asks the player for them and writes them into the Emscripten file system
# before calling main() (see web/README.md).  Only the fonts are preloaded.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN AND PPLE_BUILD_VIEW)
  # Every wasm target needs a growing heap: one resource DLL is 27 MB and the loader holds it
  # whole, so Emscripten's 16 MB default aborts with OOM before anything is drawn.
  set(PPLE_WASM_MEMORY -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sSTACK_SIZE=5242880)
  add_executable(pple-view-web ${PPLE_VIEW_SOURCES} src/viewer/jpeg.cpp)
  target_link_libraries(pple-view-web PRIVATE pple_core ${PPLE_AUDIO_TARGET})
  target_compile_options(pple-view-web PRIVATE -Wall -Wextra -Wpedantic -sUSE_LIBJPEG=1)
  target_link_options(pple-view-web PRIVATE
    -sUSE_SDL=3
    -sUSE_SDL_TTF=3
    -sUSE_LIBJPEG=1
    ${PPLE_WASM_MEMORY}
    # The page starts the session itself, once the player has chosen the game files, and keeps
    # the runtime alive so that leaving the menu tears down the session instead of the page.
    -sINVOKE_RUN=0
    -sEXIT_RUNTIME=0
    -sEXPORTED_RUNTIME_METHODS=FS,callMain
    # callMain needs _main exported; the page uses FS to write the chosen game files in.
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free
    -sENVIRONMENT=web,worker,node
    --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/web/fonts@/fonts"
  )
  # The page only exists when the link is asked for an .html: Emscripten writes the shell, the
  # JavaScript and the .wasm together, which is also what makes --shell-file take effect.
  set_target_properties(pple-view-web PROPERTIES SUFFIX ".html")
  # --shell-file is a link flag, so ninja has to be told the page is a link input; without this
  # an edited shell.html would not rebuild the emitted page.
  set_property(TARGET pple-view-web APPEND PROPERTY LINK_DEPENDS
               "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  # The headless check (web/smoke-node.mjs) drives the command-line tools from node and writes
  # the game files into the runtime's own file system before main() runs.  That needs the
  # modularized output: node's module scope already owns an identifier called Module, so a
  # generated script cannot pick a caller's Module up the way the browser page can.
  foreach(tool pple-res pple-xml)
    if(TARGET ${tool})
      target_link_options(${tool} PRIVATE
        ${PPLE_WASM_MEMORY}
        -sMODULARIZE=1
        -sEXPORT_NAME=ppleModule
        -sEXPORTED_RUNTIME_METHODS=FS)
    endif()
  endforeach()
  message(STATUS "pple-view-web: enabled (Emscripten), fonts preloaded from web/fonts")
endif()

