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

function(scraps_add_client target game)
  add_executable(${target} ${PROJECT_SOURCE_DIR}/apps/client/main.cpp)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/apps)
  target_link_libraries(${target} PRIVATE scraps_net scraps_views scraps_udp scraps_headless scraps_dynlib
                                          scraps_${game}_app)
  if(TARGET scraps_sdl3)
    target_link_libraries(${target} PRIVATE scraps_sdl3)
    target_compile_definitions(${target} PRIVATE SCRAPS_HAS_SDL3=1)
  else()
    target_compile_definitions(${target} PRIVATE SCRAPS_HAS_SDL3=0)
  endif()
  scraps_target_defaults(${target})
endfunction()

scraps_add_server(scraps_server ${SCRAPS_GAME})
scraps_add_client(scraps_client ${SCRAPS_GAME})

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

# Smoke tests: each executable runs end to end without a window. The
# sandbox's scenes by name; any other game in its default scene.
if(SCRAPS_GAME STREQUAL "sandbox")
  add_test(NAME client_offline_isometric COMMAND scraps_client --headless --frames 600 --scene isometric)
  add_test(NAME client_offline_side COMMAND scraps_client --headless --frames 600 --scene side)
  add_test(NAME client_offline_top_down COMMAND scraps_client --headless --frames 600 --scene top-down)
  add_test(NAME client_offline_crowd COMMAND scraps_client --headless --frames 120 --scene crowd)
  set(scraps_hot_game_scene --scene side)  # the autopilot keeps picking up coins there
else()
  add_test(NAME client_offline_default COMMAND scraps_client --headless --frames 600)
  set(scraps_hot_game_scene "")
endif()
add_test(NAME server_runs COMMAND scraps_server --bind 127.0.0.1 --port 47777 --ticks 30)
if(SCRAPS_HOT_RELOAD)
  # The same, with the game loaded from its module (part B, hot reload).
  add_test(NAME client_offline_hot_game COMMAND scraps_client --headless --frames 600 ${scraps_hot_game_scene} --hot-game)
  add_test(NAME server_hot_game COMMAND scraps_server --bind 127.0.0.1 --port 47778 --ticks 30 --hot-game)
else()
  # Release builds link the game statically and say so when asked for a module.
  add_test(NAME client_hot_game_unavailable COMMAND scraps_client --headless --frames 1 --hot-game)
  add_test(NAME server_hot_game_unavailable COMMAND scraps_server --ticks 1 --hot-game)
  set_tests_properties(client_hot_game_unavailable server_hot_game_unavailable PROPERTIES
                       PASS_REGULAR_EXPRESSION "--hot-game is not available in this build")
endif()
add_test(NAME bench_runs COMMAND scraps_bench --ticks 60)
