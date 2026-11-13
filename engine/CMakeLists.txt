# Engine libraries, one per layer (constitution section 3). Each library links
# only the layers below it, so a wrong-direction dependency fails to link,
# and cmake/CheckLayers.cmake also rejects wrong-direction #includes.
#
#   scraps_views  scraps_net          (views and net sit side by side)
#          \        /
#         scraps_core
#             |
#        scraps_platform  <──  scraps_headless, scraps_sdl3, scraps_udp (backends)
#
#   scraps_dynlib (backend) loads shared libraries: game modules for hot
#   reload. Only apps use it; core never does.

set(SCRAPS_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(scraps_platform STATIC
  platform/address.cpp)
target_include_directories(scraps_platform PUBLIC ${SCRAPS_ENGINE_DIR})
scraps_target_defaults(scraps_platform)

# Third-party: stb_image, compiled once, PNG only. Deliberately NOT given
# scraps_target_defaults: third-party code must not break our -Werror build.
add_library(scraps_stb STATIC
  third_party/stb/stb_image_impl.cpp)
target_include_directories(scraps_stb SYSTEM PUBLIC ${SCRAPS_ENGINE_DIR})
target_compile_features(scraps_stb PUBLIC cxx_std_17)

# Third-party: the audio decoders (dr_wav, dr_flac, dr_mp3, stb_vorbis),
# compiled once behind a small C++ face (third_party/audio_decoders.h).
add_library(scraps_audio_decoders STATIC
  third_party/audio_decoders.cpp)
target_include_directories(scraps_audio_decoders SYSTEM PUBLIC ${SCRAPS_ENGINE_DIR})
target_compile_features(scraps_audio_decoders PUBLIC cxx_std_17)

find_package(Threads REQUIRED)

add_library(scraps_core STATIC
  core/audio_convert.cpp
  core/file_watch.cpp
  core/fixed_step.cpp
  core/image.cpp
  core/jobs.cpp
  core/level.cpp
  core/mixer.cpp
  core/movement.cpp
  core/save.cpp
  core/music.cpp
  core/scene.cpp
  core/serialize.cpp
  core/simulate.cpp
  core/sound.cpp
  core/world.cpp)
target_link_libraries(scraps_core PUBLIC scraps_platform Threads::Threads PRIVATE scraps_stb scraps_audio_decoders)
scraps_target_defaults(scraps_core)

# The game-module boundary hash (cmake/ScrapsBoundaryHash.cmake): the
# contents of the sources that define what crosses between an exe and a game
# module, written into a generated header that core/game_api.h includes.
# Editing any of those files re-runs configure on the next build (even one
# that builds only the module, like the mingw-game / msvc-game presets), so
# the module gets the new hash and an exe built before refuses it.
include(ScrapsBoundaryHash)
scraps_boundary_files(SCRAPS_BOUNDARY_FILES "${PROJECT_SOURCE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SCRAPS_BOUNDARY_FILES})
scraps_boundary_hash(SCRAPS_BOUNDARY_HASH "${PROJECT_SOURCE_DIR}" ${SCRAPS_BOUNDARY_FILES})
list(LENGTH SCRAPS_BOUNDARY_FILES SCRAPS_BOUNDARY_FILE_COUNT)
scraps_boundary_header_text(SCRAPS_BOUNDARY_HEADER ${SCRAPS_BOUNDARY_HASH} ${SCRAPS_BOUNDARY_FILE_COUNT})
set(SCRAPS_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
set(SCRAPS_BOUNDARY_HEADER_FILE "${SCRAPS_GENERATED_DIR}/core/boundary_hash.h" PARENT_SCOPE)
# Written only when the text changes, so an unrelated re-configure rebuilds nothing.
file(CONFIGURE OUTPUT "${SCRAPS_GENERATED_DIR}/core/boundary_hash.h" CONTENT "${SCRAPS_BOUNDARY_HEADER}")
target_include_directories(scraps_core PUBLIC "${SCRAPS_GENERATED_DIR}")
message(STATUS "Game-module boundary hash: ${SCRAPS_BOUNDARY_HASH} (${SCRAPS_BOUNDARY_FILE_COUNT} files)")

add_library(scraps_views STATIC
  views/controls.cpp
  views/isometric_view.cpp
  views/scene_music.cpp
  views/side_view.cpp
  views/sound_bank.cpp
  views/sprite_bank.cpp
  views/top_down_view.cpp
  views/view.cpp
  views/view_factory.cpp)
target_link_libraries(scraps_views PUBLIC scraps_core)
scraps_target_defaults(scraps_views)

add_library(scraps_net STATIC
  net/client.cpp
  net/cookie.cpp
  net/loopback.cpp
  net/protocol.cpp
  net/rate_limit.cpp
  net/server.cpp)
target_link_libraries(scraps_net PUBLIC scraps_core)
scraps_target_defaults(scraps_net)

# ---------------------------------------------------------------- backends

add_library(scraps_headless STATIC
  backends/headless/headless_backend.cpp)
target_link_libraries(scraps_headless PUBLIC scraps_platform)
scraps_target_defaults(scraps_headless)

add_library(scraps_udp STATIC
  backends/udp/udp_net.cpp)
target_link_libraries(scraps_udp PUBLIC scraps_platform)
if(WIN32)
  target_link_libraries(scraps_udp PRIVATE ws2_32)
endif()
scraps_target_defaults(scraps_udp)

# Loads game modules (shared libraries) for hot reload. Apps only.
add_library(scraps_dynlib STATIC
  backends/dynlib/library.cpp)
target_include_directories(scraps_dynlib PUBLIC ${SCRAPS_ENGINE_DIR})
target_link_libraries(scraps_dynlib PRIVATE ${CMAKE_DL_LIBS})
scraps_target_defaults(scraps_dynlib)

if(SCRAPS_SDL3_TARGET)
  add_library(scraps_sdl3 STATIC
    backends/sdl3/sdl3_backend.cpp
    backends/sdl3/sdl3_devices.cpp
    backends/sdl3/sdl3_gamepad.cpp)
  target_link_libraries(scraps_sdl3 PUBLIC scraps_platform PRIVATE ${SCRAPS_SDL3_TARGET})
  scraps_target_defaults(scraps_sdl3)
endif()

# The webview2 backend (UI layout A). Its window-independent half (the frame
# format, the batch renderer, page input and the UI link) is plain C++ and
# builds everywhere, so tests run it with any compiler; the window half needs
# Windows and the WebView2 SDK (cmake/ScrapsWebView2.cmake).
add_library(scraps_webview2_core STATIC
  backends/webview2/batch_renderer.cpp
  backends/webview2/frame_format.cpp
  backends/webview2/page_input.cpp
  backends/webview2/page_link.cpp)
target_link_libraries(scraps_webview2_core PUBLIC scraps_platform)
scraps_target_defaults(scraps_webview2_core)

if(SCRAPS_WEBVIEW2_SDK)
  # renderer.js goes inside the program (kRendererJs), regenerated when it changes.
  set(scraps_renderer_js_header "${SCRAPS_GENERATED_DIR}/backends/webview2/renderer_js.h")
  add_custom_command(
    OUTPUT "${scraps_renderer_js_header}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${SCRAPS_ENGINE_DIR}/backends/webview2/renderer.js
            -DOUTPUT=${scraps_renderer_js_header} -DNAME=kRendererJs
            -P ${PROJECT_SOURCE_DIR}/cmake/ScrapsEmbedFile.cmake
    DEPENDS backends/webview2/renderer.js ${PROJECT_SOURCE_DIR}/cmake/ScrapsEmbedFile.cmake
    COMMENT "Embedding renderer.js"
    VERBATIM)
  add_library(scraps_webview2 STATIC
    backends/webview2/webview2_backend.cpp
    "${scraps_renderer_js_header}")
  target_include_directories(scraps_webview2 PRIVATE "${SCRAPS_GENERATED_DIR}")
  target_link_libraries(scraps_webview2 PUBLIC scraps_webview2_core PRIVATE scraps_webview2_sdk ole32 shlwapi shell32
                                                                         user32 uuid)
  # Windows 10 APIs (per-monitor DPI); WebView2 needs Windows 10 anyway.
  target_compile_definitions(scraps_webview2 PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
  scraps_target_defaults(scraps_webview2)
endif()
