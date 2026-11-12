add_executable(scraps_tests
  test_main.cpp
  test_core.cpp
  test_scene.cpp
  test_views.cpp
  test_net.cpp
  test_sandbox.cpp
  test_udp.cpp
  test_game.cpp)
# test_game.cpp tests the apps' hot reload (apps/common/game_module.h).
target_include_directories(scraps_tests PRIVATE ${PROJECT_SOURCE_DIR}/apps)
target_link_libraries(scraps_tests PRIVATE scraps_net scraps_views scraps_headless scraps_udp scraps_dynlib scraps_sandbox)
scraps_target_defaults(scraps_tests)
target_compile_definitions(scraps_tests PRIVATE SCRAPS_TEST_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/audio")

# Two modules that must be refused (game.mismatch_is_refused): one from
# "another" game API version, one with "another" layout.
foreach(kind version layout)
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

foreach(group core scene views platform controls net sandbox udp game)
  add_test(NAME unit_${group} COMMAND scraps_tests ${group}.)
endforeach()

add_executable(scraps_alloc_test alloc_test.cpp)
target_include_directories(scraps_alloc_test PRIVATE ${PROJECT_SOURCE_DIR}/apps)
target_link_libraries(scraps_alloc_test PRIVATE scraps_net scraps_views scraps_headless scraps_dynlib scraps_sandbox)
scraps_target_defaults(scraps_alloc_test)
# The allocation test also polls a hot game module every frame.
add_dependencies(scraps_alloc_test scraps_sandbox_module)
target_compile_definitions(scraps_alloc_test PRIVATE
  "SCRAPS_SANDBOX_MODULE_FILE=\"$<TARGET_FILE:scraps_sandbox_module>\""
  "SCRAPS_TEST_WORK_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/game_work\"")
add_test(NAME no_allocation_in_hot_path COMMAND scraps_alloc_test)
