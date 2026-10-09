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
  src/mui.cpp
  src/split_list.cpp
  src/rgmh.cpp
  src/sha256.cpp
)
target_include_directories(pple_core PUBLIC include)

if(MSVC)
  target_compile_options(pple_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(pple_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Audio: WMA decoding through Media Foundation.  Kept out of pple_core so that
# library stays dependency-free; Windows only, because MF is the decoder.
# ---------------------------------------------------------------------------
set(PPLE_AUDIO_TARGET "")
if(WIN32)
  add_library(pple_audio STATIC src/wma.cpp)
  target_include_directories(pple_audio PUBLIC include)
  target_link_libraries(pple_audio PUBLIC mfplat mfuuid mfreadwrite ole32)
  if(MSVC)
    target_compile_options(pple_audio PRIVATE /W4 /permissive-)
  else()
    target_compile_options(pple_audio PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  set(PPLE_AUDIO_TARGET pple_audio)
  message(STATUS "pple_audio: enabled (Media Foundation)")
else()
  message(STATUS "pple_audio: disabled (Media Foundation is Windows only)")
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

if(PPLE_BUILD_VIEW)
  if(EXISTS "${PPLE_SDL_ROOT}/include/SDL3/SDL.h")
    add_executable(pple-view
      src/view_main.cpp
      src/viewer/app.cpp
      src/viewer/audio.cpp
      src/viewer/browser.cpp
      src/viewer/cakes_screen.cpp
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

