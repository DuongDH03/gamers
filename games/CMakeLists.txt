set(SCRAPS_SANDBOX_SOURCES
  sandbox/sandbox.cpp
  sandbox/sandbox_api.cpp)

# The sandbox linked into programs: release builds, bench and tests use it,
# and the apps too unless started with --hot-game.
add_library(scraps_sandbox STATIC ${SCRAPS_SANDBOX_SOURCES})
target_include_directories(scraps_sandbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scraps_sandbox PUBLIC scraps_core)
# Apps find the sandbox scenes here unless --scene-dir says otherwise.
target_compile_definitions(scraps_sandbox PUBLIC
  "SCRAPS_SANDBOX_SCENE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/sandbox/scenes\"")
scraps_target_defaults(scraps_sandbox)

# The same game as a module for hot reload (spec 2026-11-12, part B): a
# running scraps_client --hot-game or scraps_server --hot-game swaps in a new
# build of it between frames. Build only this target while they run:
#   cmake --build --preset mingw-game   (or msvc-game)
# It carries its own copy of the engine code it calls (scraps_core), and
# exports one symbol, scraps_game_api (SCRAPS_GAME_MODULE turns it on).
# It lands next to the executables (MSVC: in the same per-config folder),
# named scraps_sandbox_module.dll on both compilers (no "lib" prefix).
add_library(scraps_sandbox_module SHARED ${SCRAPS_SANDBOX_SOURCES})
target_include_directories(scraps_sandbox_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scraps_sandbox_module PRIVATE scraps_core)
target_compile_definitions(scraps_sandbox_module PRIVATE SCRAPS_GAME_MODULE=1)
set_target_properties(scraps_sandbox_module PROPERTIES
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
scraps_target_defaults(scraps_sandbox_module)
