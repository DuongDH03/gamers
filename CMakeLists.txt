cmake_minimum_required(VERSION 3.21)

project(Scraps VERSION 0.1.0 DESCRIPTION "A small 2D game engine built to learn engine internals" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The engine's static libraries are also linked into the game module (a
# shared library, for hot reload), which needs position-independent code on
# Linux. Windows ignores it.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SCRAPS_WITH_SDL3 "Build the SDL3 backend and the windowed client" ON)
option(SCRAPS_FETCH_SDL3 "Download and build SDL3 when it is not installed" ON)
option(SCRAPS_BUILD_TESTS "Build the test suite" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ScrapsWarnings)
include(ScrapsSDL3)

enable_testing()

add_subdirectory(engine)
add_subdirectory(games)
add_subdirectory(apps)

if(SCRAPS_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# Rule 3 is checked mechanically: the include graph must point downward only.
add_test(NAME layer_rules
         COMMAND ${CMAKE_COMMAND} -DSCRAPS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckLayers.cmake)
# ...and the check itself is not fooled by <Windows.h> instead of <windows.h>.
add_test(NAME layer_rules_catch_mixed_case
         COMMAND ${CMAKE_COMMAND} -DSCRAPS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
                 -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/layer_rules_work
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestCheckLayers.cmake)

# The game-module boundary hash follows the sources it covers
# (cmake/ScrapsBoundaryHash.cmake), and the generated header is current.
add_test(NAME boundary_hash_tracks_sources
         COMMAND ${CMAKE_COMMAND} -DSCRAPS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
                 -DGENERATED_HEADER=${SCRAPS_BOUNDARY_HEADER_FILE}
                 -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/boundary_hash_work
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestBoundaryHash.cmake)
