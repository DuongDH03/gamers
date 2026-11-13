# The executables. Apps are the only place that names a concrete backend (Rule 3).

add_executable(scraps_server server/main.cpp)
target_include_directories(scraps_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scraps_server PRIVATE scraps_net scraps_views scraps_udp scraps_dynlib scraps_sandbox)
scraps_target_defaults(scraps_server)

add_executable(scraps_client client/main.cpp)
target_include_directories(scraps_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scraps_client PRIVATE scraps_net scraps_views scraps_udp scraps_headless scraps_dynlib scraps_sandbox)
if(TARGET scraps_sdl3)
  target_link_libraries(scraps_client PRIVATE scraps_sdl3)
  target_compile_definitions(scraps_client PRIVATE SCRAPS_HAS_SDL3=1)
else()
  target_compile_definitions(scraps_client PRIVATE SCRAPS_HAS_SDL3=0)
endif()
scraps_target_defaults(scraps_client)

# --hot-game loads the sandbox module from where the build puts it (next to
# the executables), and building the apps builds it too.
#   SCRAPS_GAME_BUILD_COMMAND: how to rebuild just that module for this
#     program's configuration, for the console and HUD hints.
#   SCRAPS_GAME_MODULE_OTHER_BUILDS: multi-config generators (Visual Studio)
#     only: where the other configurations put the module, '|'-separated. A
#     Debug exe never reloads a Release build (msvc-game builds Release), so
#     the app says so when one of those changes instead of staying silent.
get_property(scraps_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
get_target_property(scraps_module_dir scraps_sandbox_module RUNTIME_OUTPUT_DIRECTORY)
set(scraps_game_build_command "cmake --build ${CMAKE_BINARY_DIR} --target scraps_sandbox_module")
set(scraps_other_builds "")
if(scraps_multi_config)
  set(scraps_game_build_command "cmake --build ${CMAKE_BINARY_DIR} --config $<CONFIG> --target scraps_sandbox_module")
  foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(APPEND scraps_other_builds
           "$<$<NOT:$<CONFIG:${config}>>:${scraps_module_dir}/${config}/$<TARGET_FILE_NAME:scraps_sandbox_module>|>")
  endforeach()
endif()
foreach(app scraps_server scraps_client)
  target_compile_definitions(${app} PRIVATE
    "SCRAPS_GAME_MODULE_FILE=\"$<TARGET_FILE:scraps_sandbox_module>\""
    "SCRAPS_GAME_BUILD_COMMAND=\"${scraps_game_build_command}\""
    "SCRAPS_GAME_MODULE_OTHER_BUILDS=\"${scraps_other_builds}\"")
  add_dependencies(${app} scraps_sandbox_module)
endforeach()

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

# Smoke tests: each executable runs end to end without a window.
add_test(NAME client_offline_isometric COMMAND scraps_client --headless --frames 600 --scene isometric)
add_test(NAME client_offline_side COMMAND scraps_client --headless --frames 600 --scene side)
add_test(NAME client_offline_top_down COMMAND scraps_client --headless --frames 600 --scene top-down)
add_test(NAME client_offline_crowd COMMAND scraps_client --headless --frames 120 --scene crowd)
add_test(NAME server_runs COMMAND scraps_server --bind 127.0.0.1 --port 47777 --ticks 30)
# The same, with the game loaded from its module (part B, hot reload).
add_test(NAME client_offline_hot_game COMMAND scraps_client --headless --frames 600 --scene side --hot-game)
add_test(NAME server_hot_game COMMAND scraps_server --bind 127.0.0.1 --port 47778 --ticks 30 --hot-game)
add_test(NAME bench_runs COMMAND scraps_bench --ticks 60)

# Save and load, and pause (docs/specs/2026-11-14-offline-essentials-and-ui.md,
# parts 1 and 4), each in a folder of its own. The dedicated server saves
# when it stops and starts again from that save; the offline client's
# autopilot quick-saves (F5), pauses for a second (P twice) and loads (F9).
set(scraps_save_test_dir "${CMAKE_CURRENT_BINARY_DIR}/save_test")
file(MAKE_DIRECTORY "${scraps_save_test_dir}")
add_test(NAME server_save
         COMMAND scraps_server --bind 127.0.0.1 --port 47779 --ticks 30 --scene top-down
                 --save "${scraps_save_test_dir}/server.sav")
set_tests_properties(server_save PROPERTIES FIXTURES_SETUP server_save_file
                     PASS_REGULAR_EXPRESSION "saved tick 30 to" FAIL_REGULAR_EXPRESSION "error:")
add_test(NAME server_load
         COMMAND scraps_server --bind 127.0.0.1 --port 47780 --ticks 30 --load "${scraps_save_test_dir}/server.sav")
set_tests_properties(server_load PROPERTIES FIXTURES_REQUIRED server_save_file
                     PASS_REGULAR_EXPRESSION "scene 'top-down', tick 30.*world tick 60" FAIL_REGULAR_EXPRESSION "error:")
add_test(NAME client_offline_save_pause_load COMMAND scraps_client --headless --frames 600 --scene top-down
         WORKING_DIRECTORY "${scraps_save_test_dir}")
set_tests_properties(client_offline_save_pause_load PROPERTIES
                     PASS_REGULAR_EXPRESSION "saves 1, loads 1, save errors 0, paused frames 60, ticks while paused 0"
                     FAIL_REGULAR_EXPRESSION "FAILED|error:|failed:")
