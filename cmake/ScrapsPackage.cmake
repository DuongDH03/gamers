# A release of the selected game (spec docs/specs/2026-11-14-offline-essentials-and-ui.md,
# part 5): `cmake --install` lays it out, CPack zips it.
#
#   <prefix>/scraps_client.exe, scraps_server.exe
#            scraps_web.exe and ui/ (the React UI), plus WebView2Loader.dll
#            with MinGW, when scraps_web is built (apps/web/CMakeLists.txt)
#            the DLLs they need that Windows does not have: the compiler's
#            runtime (MinGW: libstdc++-6, libgcc_s_seh-1, libwinpthread-1;
#            MSVC: the VC runtime) and SDL3.dll when SDL3 is a shared library
#            (the bundled SDL3 is linked statically, so usually there is none)
#            scenes/      the game's whole scenes folder (art, sounds, music)
#            README.txt   how to run it, keys, command-line options
#
# The zip is Scraps-<game>-<version>-win64.zip. Release presets build with
# SCRAPS_HOT_RELOAD off, so the apps link the game statically and no module
# is installed. The `package_runs` test (tests/CMakeLists.txt) installs into
# a temporary folder and runs the client from there with only Windows' own
# folders on PATH.

set(SCRAPS_PACKAGE_SUFFIX "${CMAKE_SYSTEM_NAME}")
if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SCRAPS_PACKAGE_SUFFIX win64)
  else()
    set(SCRAPS_PACKAGE_SUFFIX win32)
  endif()
endif()
set(SCRAPS_PACKAGE_NAME "Scraps-${SCRAPS_GAME}-${PROJECT_VERSION}-${SCRAPS_PACKAGE_SUFFIX}")

install(TARGETS scraps_client scraps_server RUNTIME DESTINATION .)

# The whole folder, whatever is in it, so new scenes, art, sounds and music
# ship without touching this file.
get_property(scraps_scene_dir GLOBAL PROPERTY SCRAPS_GAME_${SCRAPS_GAME}_SCENES)
install(DIRECTORY "${scraps_scene_dir}/" DESTINATION scenes)

# README.txt: the engine's part (cmake/README.txt.in) plus the game's own
# notes (scraps_add_game's README), if it has any.
get_property(scraps_game_readme GLOBAL PROPERTY SCRAPS_GAME_${SCRAPS_GAME}_README)
set(SCRAPS_README_GAME_NOTES "")
if(scraps_game_readme)
  file(READ "${scraps_game_readme}" SCRAPS_README_GAME_NOTES)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${scraps_game_readme}")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/README.txt.in" "${PROJECT_BINARY_DIR}/package/README.txt" @ONLY
               NEWLINE_STYLE CRLF)  # for Notepad
install(FILES "${PROJECT_BINARY_DIR}/package/README.txt" DESTINATION .)

# The DLLs the executables load, found the way Windows will look for them,
# with the compiler's own folder searched before PATH (another libstdc++ on
# PATH, Git's for example, must not be the one shipped). Windows' own DLLs
# are left out. An install script, so it reads the executables just built.
if(WIN32)
  set(scraps_runtime_dirs "")
  set(scraps_runtime_tool "")
  set(scraps_runtime_command "")
  if(MSVC)
    # dumpbin sits next to the linker.
    get_filename_component(scraps_linker_dir "${CMAKE_LINKER}" DIRECTORY)
    set(scraps_runtime_tool dumpbin)
    set(scraps_runtime_command "${scraps_linker_dir}/dumpbin.exe")
  else()
    get_filename_component(scraps_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    list(APPEND scraps_runtime_dirs "${scraps_compiler_dir}")
    set(scraps_runtime_tool objdump)
    set(scraps_runtime_command "${CMAKE_OBJDUMP}")
  endif()
  if(TARGET SDL3::SDL3-shared)
    list(APPEND scraps_runtime_dirs "$<TARGET_FILE_DIR:SDL3::SDL3-shared>")
  endif()
  set(scraps_runtime_executables "$<TARGET_FILE:scraps_client>;$<TARGET_FILE:scraps_server>")
  if(TARGET scraps_web)
    string(APPEND scraps_runtime_executables ";$<TARGET_FILE:scraps_web>")  # apps/web/CMakeLists.txt installs it
  endif()
  install(CODE "
    set(SCRAPS_RUNTIME_EXECUTABLES \"${scraps_runtime_executables}\")
    set(SCRAPS_RUNTIME_DIRS \"${scraps_runtime_dirs}\")
    set(SCRAPS_RUNTIME_TOOL \"${scraps_runtime_tool}\")
    set(SCRAPS_RUNTIME_COMMAND \"${scraps_runtime_command}\")
    include(\"${CMAKE_CURRENT_LIST_DIR}/ScrapsInstallRuntime.cmake\")
  ")
endif()

# MSVC: the VC runtime DLLs (vcruntime140, vcruntime140_1, msvcp140), which
# a clean Windows does not have. Windows 10 and 11 already carry the UCRT.
if(MSVC)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
  set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
  include(InstallRequiredSystemLibraries)
endif()

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "Scraps-${SCRAPS_GAME}")
set(CPACK_PACKAGE_VENDOR "Scraps")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_FILE_NAME "${SCRAPS_PACKAGE_NAME}")
# The zip holds one folder of the same name, so unzipping never scatters files.
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/package")
set(CPACK_SOURCE_GENERATOR "")
include(CPack)

if(NOT SCRAPS_HOT_RELOAD)
  message(STATUS "Scraps: release package ${SCRAPS_PACKAGE_NAME}.zip (game linked statically)")
endif()
