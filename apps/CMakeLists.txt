# The executables. Apps are the only place that names a concrete backend (Rule 3).

# scraps_add_server(<target> <game>) and scraps_add_client(<target> <game>):
# the dedicated server and the player's executable, built for one game
# (cmake/ScrapsGame.cmake). The apps take the game from the generated
# "selected_game.h" and never name it; tests build a client for a second game.
function(scraps_add_server target game)
  add_executable(${target} ${PROJECT_SOURCE_DIR}/apps/server/main.cpp)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/apps)
  target_link_libraries(${target} PRIVATE scraps_net scraps_views scraps_udp scraps_dynlib scraps_${game}_app)
  scraps_target_defaults(${target})
endfunction()

#
# The client is the executable (main.cpp: options and the loop) plus
# <target>_lib (client_app.cpp: ClientApp, one frame of the client), which
# tests and other hosts (a WebView app) link too.
function(scraps_add_client target game)
  add_library(${target}_lib STATIC ${PROJECT_SOURCE_DIR}/apps/client/client_app.cpp)
  target_include_directories(${target}_lib PUBLIC ${PROJECT_SOURCE_DIR}/apps)
  target_link_libraries(${target}_lib PUBLIC scraps_net scraps_views scraps_udp scraps_headless scraps_dynlib
                                             scraps_ui_model scraps_${game}_app)
  if(TARGET scraps_sdl3)
    target_link_libraries(${target}_lib PUBLIC scraps_sdl3)
    target_compile_definitions(${target}_lib PUBLIC SCRAPS_HAS_SDL3=1)
  else()
    target_compile_definitions(${target}_lib PUBLIC SCRAPS_HAS_SDL3=0)
  endif()
  scraps_target_defaults(${target}_lib)

  add_executable(${target} ${PROJECT_SOURCE_DIR}/apps/client/main.cpp)
  target_link_libraries(${target} PRIVATE ${target}_lib)
  scraps_target_defaults(${target})
endfunction()

# The app side of the UI bridge (docs/specs/2026-11-14-offline-essentials-
# and-ui.md, part 6a): the tiny JSON writer and reader, and the UI model.
add_library(scraps_ui_model STATIC common/ui_json.cpp common/ui_model.cpp)
target_include_directories(scraps_ui_model PUBLIC ${PROJECT_SOURCE_DIR}/apps)
target_link_libraries(scraps_ui_model PUBLIC scraps_net scraps_views scraps_dynlib)
scraps_target_defaults(scraps_ui_model)

scraps_add_server(scraps_server ${SCRAPS_GAME})
scraps_add_client(scraps_client ${SCRAPS_GAME})
# The client with the React UI in a WebView2 window (UI layout A).
add_subdirectory(web)

# The bench measures the sandbox's rules on the sandbox's scenes.
add_executable(scraps_bench bench/main.cpp)
target_link_libraries(scraps_bench PRIVATE scraps_views scraps_headless scraps_sandbox)
scraps_target_defaults(scraps_bench)

add_executable(scraps_race_demo race_demo/main.cpp)
target_link_libraries(scraps_race_demo PRIVATE scraps_core)
scraps_target_defaults(scraps_race_demo)

# Stage 8's "race-condition test that fails without locks": the atomic run
# must pass; the plain run must lose updates (so CTest expects it to fail).
# On a one-core machine there is no race to show, and the test is skipped.
add_test(NAME race_demo_atomic_is_exact COMMAND scraps_race_demo)
add_test(NAME race_demo_plain_loses_updates COMMAND scraps_race_demo --unsafe)
set_tests_properties(race_demo_plain_loses_updates PROPERTIES WILL_FAIL TRUE SKIP_RETURN_CODE 77)
set_tests_properties(race_demo_atomic_is_exact PROPERTIES SKIP_RETURN_CODE 77)
# The race shows only when the threads really run at the same time: under
# ctest -j the other tests can keep the cores so busy that they take turns.
set_tests_properties(race_demo_atomic_is_exact race_demo_plain_loses_updates PROPERTIES RUN_SERIAL TRUE)

# Smoke tests: each executable runs end to end without a window. The
# sandbox's scenes by name; any other game in its default scene.
#
# They run in parallel (ctest -j), and next to another build's ctest, so
# nothing may be shared between them:
#   - servers bind --port 0 (a free port the system picks), never a fixed one;
#   - each client runs in a folder of its own, since the headless autopilot
#     quick-saves (F5) into saves/quick.sav under its working folder.
# Release builds (SCRAPS_HOT_RELOAD off) compile in no source scenes folder
# (cmake/ScrapsGame.cmake), so their tests name it with --scene-dir.
get_property(scraps_game_scenes GLOBAL PROPERTY SCRAPS_GAME_${SCRAPS_GAME}_SCENES)
set(scraps_scene_dir_args "")
if(NOT SCRAPS_HOT_RELOAD)
  set(scraps_scene_dir_args --scene-dir "${scraps_game_scenes}")
endif()
set(scraps_smoke_dir "${CMAKE_CURRENT_BINARY_DIR}/smoke")
# scraps_add_client_smoke(<name> <args>...): the client in smoke/<name>/.
function(scraps_add_client_smoke name)
  file(MAKE_DIRECTORY "${scraps_smoke_dir}/${name}")
  add_test(NAME ${name} COMMAND scraps_client ${ARGN} ${scraps_scene_dir_args}
           WORKING_DIRECTORY "${scraps_smoke_dir}/${name}")
endfunction()

if(SCRAPS_GAME STREQUAL "sandbox")
  scraps_add_client_smoke(client_offline_isometric --headless --frames 600 --scene isometric)
  scraps_add_client_smoke(client_offline_side --headless --frames 600 --scene side)
  scraps_add_client_smoke(client_offline_top_down --headless --frames 600 --scene top-down)
  scraps_add_client_smoke(client_offline_crowd --headless --frames 120 --scene crowd)
  set(scraps_hot_game_scene --scene side)  # the autopilot keeps picking up coins there
else()
  scraps_add_client_smoke(client_offline_default --headless --frames 600)
  set(scraps_hot_game_scene "")
endif()
add_test(NAME server_runs COMMAND scraps_server --bind 127.0.0.1 --port 0 --ticks 30 ${scraps_scene_dir_args})
# The port the system picked is printed: it must not be 0.
set_tests_properties(server_runs PROPERTIES
                     PASS_REGULAR_EXPRESSION "listening on 127\\.0\\.0\\.1:[1-9][0-9]*,.*stopped after 30 ticks"
                     FAIL_REGULAR_EXPRESSION "error:")
if(SCRAPS_HOT_RELOAD)
  # The same, with the game loaded from its module (part B, hot reload).
  scraps_add_client_smoke(client_offline_hot_game --headless --frames 600 ${scraps_hot_game_scene} --hot-game)
  add_test(NAME server_hot_game COMMAND scraps_server --bind 127.0.0.1 --port 0 --ticks 30 --hot-game)
else()
  # Release builds link the game statically and say so when asked for a module.
  add_test(NAME client_hot_game_unavailable COMMAND scraps_client --headless --frames 1 --hot-game)
  add_test(NAME server_hot_game_unavailable COMMAND scraps_server --ticks 1 --hot-game)
  set_tests_properties(client_hot_game_unavailable server_hot_game_unavailable PROPERTIES
                       PASS_REGULAR_EXPRESSION "--hot-game is not available in this build")
endif()
add_test(NAME bench_runs COMMAND scraps_bench --ticks 60)

# Save and load, and pause (docs/specs/2026-11-14-offline-essentials-and-ui.md,
# parts 1 and 4), each in a folder of its own. The dedicated server saves
# when it stops and starts again from that save; the offline client's
# autopilot quick-saves (F5), pauses for a second (P twice) and loads (F9).
set(scraps_save_test_dir "${CMAKE_CURRENT_BINARY_DIR}/save_test")
file(MAKE_DIRECTORY "${scraps_save_test_dir}")
add_test(NAME server_save
         COMMAND scraps_server --bind 127.0.0.1 --port 0 --ticks 30 --scene top-down ${scraps_scene_dir_args}
                 --save "${scraps_save_test_dir}/server.sav")
set_tests_properties(server_save PROPERTIES FIXTURES_SETUP server_save_file
                     PASS_REGULAR_EXPRESSION "saved tick 30 to" FAIL_REGULAR_EXPRESSION "error:")
add_test(NAME server_load
         COMMAND scraps_server --bind 127.0.0.1 --port 0 --ticks 30 ${scraps_scene_dir_args}
                 --load "${scraps_save_test_dir}/server.sav")
set_tests_properties(server_load PROPERTIES FIXTURES_REQUIRED server_save_file
                     PASS_REGULAR_EXPRESSION "scene 'top-down', tick 30.*world tick 60" FAIL_REGULAR_EXPRESSION "error:")
# The same through a folder whose name is not ASCII: the server reads its
# command line as UTF-8 (dynlib::utf8_arguments), not in the ANSI code page,
# where these characters would turn into '?'.
set(scraps_unicode_save "${scraps_save_test_dir}/Phương 模块/server.sav")
add_test(NAME server_save_non_ascii
         COMMAND scraps_server --bind 127.0.0.1 --port 0 --ticks 20 --scene side ${scraps_scene_dir_args}
                 --save "${scraps_unicode_save}")
set_tests_properties(server_save_non_ascii PROPERTIES FIXTURES_SETUP server_save_non_ascii_file
                     PASS_REGULAR_EXPRESSION "saved tick 20 to" FAIL_REGULAR_EXPRESSION "error:")
add_test(NAME server_load_non_ascii
         COMMAND scraps_server --bind 127.0.0.1 --port 0 --ticks 10 ${scraps_scene_dir_args}
                 --load "${scraps_unicode_save}")
set_tests_properties(server_load_non_ascii PROPERTIES FIXTURES_REQUIRED server_save_non_ascii_file
                     PASS_REGULAR_EXPRESSION "scene 'side', tick 20.*world tick 30" FAIL_REGULAR_EXPRESSION "error:")
add_test(NAME client_offline_save_pause_load COMMAND scraps_client --headless --frames 600 --scene top-down
                                                     ${scraps_scene_dir_args}
         WORKING_DIRECTORY "${scraps_save_test_dir}")
set_tests_properties(client_offline_save_pause_load PROPERTIES
                     PASS_REGULAR_EXPRESSION "saves 1, loads 1, save errors 0, paused frames 60, ticks while paused 0"
                     FAIL_REGULAR_EXPRESSION "FAILED|error:|failed:")
