# Games, and the choice of which game the apps are built for
# (spec docs/specs/2026-11-14-offline-essentials-and-ui.md, part 5).
#
#   scraps_add_game(<name>
#                   SOURCES <file>...         the game's .cpp files
#                   SCENES <dir>              its scenes folder (scenes, art, sounds, music)
#                   [DEFAULT_SCENE <scene>]   played without --scene (default: the
#                                             first .scene file in SCENES)
#                   [HEADER <path>]           declares the GameApi function
#                                             (default: <name>/<name>.h)
#                   [API_FUNCTION <fn>]       returns the game's GameApi
#                                             (default: <name>::game_api)
#                   [INCLUDE_DIR <dir>]       where HEADER is found
#                                             (default: the calling CMakeLists' folder)
#                   [README <file>])          text about the game for the release README
#
# <name> is a C identifier in lower case (it becomes a target, macro and
# namespace name). The call makes:
#
#   scraps_<name>         the game as a static library: release builds, bench,
#                         tests, and the apps when started without --hot-game.
#                         Defines SCRAPS_<NAME>_SCENE_DIR (the source scenes folder).
#   scraps_<name>_module  the same sources as a hot-reload module (a DLL that
#                         a running app swaps in), when SCRAPS_HOT_RELOAD or
#                         SCRAPS_BUILD_TESTS is on. It lands next to the apps.
#   scraps_<name>_app     what an app links to run this game: a generated
#                         header "selected_game.h" (scraps_apps::selected_game()
#                         and selected_game_api()), so apps never name the game.
#
# The apps are built for SCRAPS_GAME (scraps_select_game() below). A second
# game is a new folder with one scraps_add_game() call, plus -DSCRAPS_GAME=<name>.

set(SCRAPS_GAME_TEMPLATE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(scraps_add_game name)
  cmake_parse_arguments(PARSE_ARGV 1 game "" "SCENES;DEFAULT_SCENE;HEADER;API_FUNCTION;INCLUDE_DIR;README" "SOURCES")
  if(NOT name MATCHES "^[a-z][a-z0-9_]*$")
    message(FATAL_ERROR "scraps_add_game: '${name}' must be lower-case letters, digits and '_', starting with a letter")
  endif()
  if(NOT game_SOURCES OR NOT game_SCENES)
    message(FATAL_ERROR "scraps_add_game(${name}): SOURCES and SCENES are required")
  endif()
  get_filename_component(scene_dir "${game_SCENES}" ABSOLUTE)
  if(NOT IS_DIRECTORY "${scene_dir}")
    message(FATAL_ERROR "scraps_add_game(${name}): no scenes folder at ${scene_dir}")
  endif()
  if(NOT game_DEFAULT_SCENE)
    file(GLOB scene_files RELATIVE "${scene_dir}" "${scene_dir}/*.scene")
    if(NOT scene_files)
      message(FATAL_ERROR "scraps_add_game(${name}): no .scene file in ${scene_dir}")
    endif()
    list(SORT scene_files)
    list(GET scene_files 0 first_scene)
    get_filename_component(game_DEFAULT_SCENE "${first_scene}" NAME_WE)
  endif()
  if(NOT EXISTS "${scene_dir}/${game_DEFAULT_SCENE}.scene")
    message(FATAL_ERROR "scraps_add_game(${name}): no scene ${scene_dir}/${game_DEFAULT_SCENE}.scene")
  endif()
  if(NOT game_HEADER)
    set(game_HEADER "${name}/${name}.h")
  endif()
  if(NOT game_API_FUNCTION)
    set(game_API_FUNCTION "${name}::game_api")
  endif()
  if(NOT game_INCLUDE_DIR)
    set(game_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  string(TOUPPER "${name}" upper)

  # The game linked into programs.
  add_library(scraps_${name} STATIC ${game_SOURCES})
  target_include_directories(scraps_${name} PUBLIC "${game_INCLUDE_DIR}")
  target_link_libraries(scraps_${name} PUBLIC scraps_core)
  # Bench and tests find this game's scenes here.
  target_compile_definitions(scraps_${name} PUBLIC "SCRAPS_${upper}_SCENE_DIR=\"${scene_dir}\"")
  scraps_target_defaults(scraps_${name})

  # The same game as a module for hot reload (spec 2026-11-12, part B): a
  # running scraps_client --hot-game or scraps_server --hot-game swaps in a
  # new build of it between frames. Build only this target while they run:
  #   cmake --build --preset mingw-game   (or msvc-game)
  # It carries its own copy of the engine code it calls (scraps_core), and
  # exports one symbol, scraps_game_api (SCRAPS_GAME_MODULE turns it on).
  # It lands next to the executables (MSVC: in the same per-config folder),
  # named scraps_<name>_module.dll on both compilers (no "lib" prefix).
  # Release builds (SCRAPS_HOT_RELOAD off) build it only for the tests, and
  # never install it.
  set(module scraps_${name}_module)
  if(SCRAPS_HOT_RELOAD OR SCRAPS_BUILD_TESTS)
    add_library(${module} SHARED ${game_SOURCES})
    target_include_directories(${module} PRIVATE "${game_INCLUDE_DIR}")
    target_link_libraries(${module} PRIVATE scraps_core)
    target_compile_definitions(${module} PRIVATE SCRAPS_GAME_MODULE=1)
    set_target_properties(${module} PROPERTIES
      PREFIX ""
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps
      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
    scraps_target_defaults(${module})
  endif()

  # What the apps link: the generated "selected_game.h" and its .cpp. The
  # header is the same text for every game except the name; the .cpp is
  # where this game's API function is named.
  set(generated "${CMAKE_BINARY_DIR}/generated/games/${name}")
  set(SCRAPS_GAME_NAME "${name}")
  set(SCRAPS_GAME_DEFAULT_SCENE "${game_DEFAULT_SCENE}")
  set(SCRAPS_GAME_HEADER "${game_HEADER}")
  set(SCRAPS_GAME_API_FUNCTION "${game_API_FUNCTION}")
  configure_file("${SCRAPS_GAME_TEMPLATE_DIR}/selected_game.h.in" "${generated}/selected_game.h" @ONLY)
  configure_file("${SCRAPS_GAME_TEMPLATE_DIR}/selected_game.cpp.in" "${generated}/selected_game.cpp" @ONLY)
  add_library(scraps_${name}_app STATIC "${generated}/selected_game.cpp")
  target_include_directories(scraps_${name}_app PUBLIC "${generated}")
  target_link_libraries(scraps_${name}_app PUBLIC scraps_core PRIVATE scraps_${name})
  scraps_target_defaults(scraps_${name}_app)

  # --hot-game loads the module from where the build puts it (next to the
  # executables), and building an app builds the module too.
  #   SCRAPS_GAME_BUILD_COMMAND: how to rebuild just that module for this
  #     program's configuration, for the console and HUD hints.
  #   SCRAPS_GAME_MODULE_OTHER_BUILDS: multi-config generators (Visual Studio)
  #     only: where the other configurations put the module, '|'-separated. A
  #     Debug exe never reloads a Release build (msvc-game builds Release), so
  #     the app says so when one of those changes instead of staying silent.
  if(SCRAPS_HOT_RELOAD)
    get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    get_target_property(module_dir ${module} RUNTIME_OUTPUT_DIRECTORY)
    set(build_command "cmake --build ${CMAKE_BINARY_DIR} --target ${module}")
    set(other_builds "")
    if(multi_config)
      set(build_command "cmake --build ${CMAKE_BINARY_DIR} --config $<CONFIG> --target ${module}")
      foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(APPEND other_builds
               "$<$<NOT:$<CONFIG:${config}>>:${module_dir}/${config}/$<TARGET_FILE_NAME:${module}>|>")
      endforeach()
    endif()
    target_compile_definitions(scraps_${name}_app PRIVATE
      SCRAPS_GAME_HOT_RELOAD=1
      "SCRAPS_GAME_MODULE_FILE=\"$<TARGET_FILE:${module}>\""
      "SCRAPS_GAME_BUILD_COMMAND=\"${build_command}\""
      "SCRAPS_GAME_MODULE_OTHER_BUILDS=\"${other_builds}\"")
    add_dependencies(scraps_${name}_app ${module})
  else()
    target_compile_definitions(scraps_${name}_app PRIVATE
      SCRAPS_GAME_HOT_RELOAD=0
      "SCRAPS_GAME_MODULE_FILE=\"\""
      "SCRAPS_GAME_BUILD_COMMAND=\"\""
      "SCRAPS_GAME_MODULE_OTHER_BUILDS=\"\"")
  endif()
  target_compile_definitions(scraps_${name}_app PRIVATE "SCRAPS_GAME_SOURCE_SCENE_DIR=\"${scene_dir}\"")

  # Remembered for the selection and the install rules (ScrapsPackage.cmake).
  set_property(GLOBAL APPEND PROPERTY SCRAPS_GAMES ${name})
  set_property(GLOBAL PROPERTY SCRAPS_GAME_${name}_SCENES "${scene_dir}")
  if(game_README)
    get_filename_component(readme "${game_README}" ABSOLUTE)
    set_property(GLOBAL PROPERTY SCRAPS_GAME_${name}_README "${readme}")
  endif()
endfunction()

# Checks that SCRAPS_GAME names a game added with scraps_add_game(), and
# makes `scraps_game_module`: the selected game's module, the one target the
# mingw-game / msvc-game presets build (presets cannot read SCRAPS_GAME).
function(scraps_select_game)
  get_property(games GLOBAL PROPERTY SCRAPS_GAMES)
  if(NOT SCRAPS_GAME IN_LIST games)
    list(JOIN games ", " known)
    message(FATAL_ERROR "SCRAPS_GAME is '${SCRAPS_GAME}', but no such game was added; known games: ${known}")
  endif()
  if(TARGET scraps_${SCRAPS_GAME}_module)
    add_custom_target(scraps_game_module)
    add_dependencies(scraps_game_module scraps_${SCRAPS_GAME}_module)
  endif()
  message(STATUS "Scraps: apps are built for the game '${SCRAPS_GAME}' (hot reload: ${SCRAPS_HOT_RELOAD})")
endfunction()
