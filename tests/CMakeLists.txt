add_executable(scraps_tests
  test_main.cpp
  test_core.cpp
  test_scene.cpp
  test_views.cpp
  test_net.cpp
  test_sandbox.cpp
  test_udp.cpp
  test_game.cpp
  test_save.cpp
  test_apps.cpp
  test_music.cpp
  test_ui.cpp
  test_webview2.cpp)
# test_game.cpp tests the apps' hot reload (apps/common/game_module.h).
target_include_directories(scraps_tests PRIVATE ${PROJECT_SOURCE_DIR}/apps)
# test_ui.cpp drives the client app itself (ClientApp, built for SCRAPS_GAME) with a headless UI.
target_link_libraries(scraps_tests PRIVATE scraps_net scraps_views scraps_headless scraps_udp scraps_dynlib scraps_sandbox
                                           scraps_ui_model scraps_client_lib scraps_webview2_core)
scraps_target_defaults(scraps_tests)
target_compile_definitions(scraps_tests PRIVATE SCRAPS_TEST_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/audio")

# Three modules that must be refused (game.mismatch_is_refused): one from
# a newer game API version, one from the version before (without UI
# commands), one with "another" layout.
foreach(kind version old layout)
  string(TOUPPER ${kind} KIND)
  add_library(scraps_test_bad_${kind}_module SHARED bad_game_module.cpp)
  target_link_libraries(scraps_test_bad_${kind}_module PRIVATE scraps_core)
  target_compile_definitions(scraps_test_bad_${kind}_module PRIVATE SCRAPS_BAD_${KIND}=1)
  set_target_properties(scraps_test_bad_${kind}_module PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
  scraps_target_defaults(scraps_test_bad_${kind}_module)
  add_dependencies(scraps_tests scraps_test_bad_${kind}_module)
  target_compile_definitions(scraps_tests PRIVATE
    "SCRAPS_BAD_${KIND}_MODULE_FILE=\"$<TARGET_FILE:scraps_test_bad_${kind}_module>\"")
endforeach()

# A module that must be accepted and whose code visibly differs from the
# sandbox's (game.swap_runs_the_new_code): the sandbox rules plus a marker on
# every player. It links the static sandbox, which does not define
# scraps_game_api, so its own export is the only one.
add_library(scraps_test_marker_module SHARED marker_game_module.cpp)
target_link_libraries(scraps_test_marker_module PRIVATE scraps_sandbox)
set_target_properties(scraps_test_marker_module PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
scraps_target_defaults(scraps_test_marker_module)
add_dependencies(scraps_tests scraps_test_marker_module)
target_compile_definitions(scraps_tests PRIVATE
  "SCRAPS_MARKER_MODULE_FILE=\"$<TARGET_FILE:scraps_test_marker_module>\"")

# The game tests load the real sandbox module from wherever this build put it
# (MSVC: a per-config folder; $<TARGET_FILE> names the right one), and work
# on copies of it in their own folder.
add_dependencies(scraps_tests scraps_sandbox_module)
target_compile_definitions(scraps_tests PRIVATE
  "SCRAPS_SANDBOX_MODULE_FILE=\"$<TARGET_FILE:scraps_sandbox_module>\""
  "SCRAPS_TEST_WORK_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/game_work\"")

foreach(group core scene views platform controls net sandbox udp game save pause apps ui webview2)
  add_test(NAME unit_${group} COMMAND scraps_tests ${group}.)
endforeach()

# The webview2 page script (renderer.js) in Node: it decodes the frame the
# C++ encoder wrote in unit_webview2 (webview2.frame_round_trips), refuses
# damaged ones, and follows the input rules (Esc never forwarded).
if(SCRAPS_NODE)
  add_test(NAME webview2_renderer_js COMMAND "${SCRAPS_NODE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/webview2_renderer.test.js")
  set_tests_properties(unit_webview2 PROPERTIES FIXTURES_SETUP webview2_frame)
  set_tests_properties(webview2_renderer_js PROPERTIES
                       ENVIRONMENT "SCRAPS_WEBVIEW2_FIXTURES=${CMAKE_CURRENT_BINARY_DIR}/game_work/webview2"
                       FIXTURES_REQUIRED webview2_frame LABELS js)
endif()
# The React app's own tests (Vitest), once the build has installed its packages.
if(SCRAPS_UI_TARGET AND SCRAPS_NODE)
  add_test(NAME ui_tests COMMAND "${SCRAPS_NODE}" "${PROJECT_SOURCE_DIR}/ui/node_modules/vitest/vitest.mjs" run
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/ui")
  set_tests_properties(ui_tests PROPERTIES LABELS "js;ui" TIMEOUT 300)
endif()

add_executable(scraps_alloc_test alloc_test.cpp)
target_include_directories(scraps_alloc_test PRIVATE ${PROJECT_SOURCE_DIR}/apps)
target_link_libraries(scraps_alloc_test PRIVATE scraps_net scraps_views scraps_headless scraps_dynlib scraps_sandbox
                                                scraps_ui_model scraps_webview2_core)
scraps_target_defaults(scraps_alloc_test)
# It also streams music from the audio test files.
target_compile_definitions(scraps_alloc_test PRIVATE SCRAPS_TEST_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/audio")
# The allocation test also polls a hot game module every frame.
add_dependencies(scraps_alloc_test scraps_sandbox_module)
target_compile_definitions(scraps_alloc_test PRIVATE
  "SCRAPS_SANDBOX_MODULE_FILE=\"$<TARGET_FILE:scraps_sandbox_module>\""
  "SCRAPS_TEST_WORK_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/game_work\"")
add_test(NAME no_allocation_in_hot_path COMMAND scraps_alloc_test)

# A game other than the sandbox (tests/tiny_game): the same client source,
# built for it through scraps_add_game() and its generated selected_game.h.
scraps_add_client(scraps_client_tiny tiny)
add_test(NAME tiny_game_runs COMMAND scraps_client_tiny --headless --frames 120)
# The HUD text comes from the tiny game's code; its scene from its own folder.
set_tests_properties(tiny_game_runs PROPERTIES
                     PASS_REGULAR_EXPRESSION "game 'tiny', scenes from [^\n]*tiny_game/scenes.*headless run.*tiny game"
                     FAIL_REGULAR_EXPRESSION "FAILED|error:|failed:")

# The release: install it into a scratch folder and run it from there with
# PATH cut down to Windows' own folders, so a DLL missing from the install
# fails the test (cmake/TestPackageRuns.cmake). Takes a few seconds.
if(WIN32)
  set(scraps_expect_web OFF)
  set(scraps_expect_loader OFF)
  if(TARGET scraps_web)
    set(scraps_expect_web ON)
    if(SCRAPS_WEBVIEW2_LOADER_DLL)
      set(scraps_expect_loader ON)
    endif()
  endif()
  add_test(NAME package_runs
           COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${PROJECT_BINARY_DIR} -DCONFIG=$<CONFIG>
                   -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/package_work -DEXPECT_WEB=${scraps_expect_web}
                   -DEXPECT_WEBVIEW2_LOADER=${scraps_expect_loader}
                   -P ${PROJECT_SOURCE_DIR}/cmake/TestPackageRuns.cmake)
  set_tests_properties(package_runs PROPERTIES LABELS package)
endif()
