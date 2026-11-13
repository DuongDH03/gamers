# scraps_web: the client with the React UI in a WebView2 window (UI layout A).
# Built when the WebView2 SDK and SDL3 (for sound and the gamepad) are there.

if(NOT SCRAPS_WEBVIEW2_SDK OR NOT TARGET scraps_webview2 OR NOT TARGET scraps_sdl3)
  message(STATUS "Scraps: scraps_web is not built (needs Windows, the WebView2 SDK and SDL3)")
  return()
endif()

# A Windows GUI program (no console window when double-clicked); it writes
# to the console it was started from, if any (attach_parent_console).
add_executable(scraps_web WIN32 main.cpp)
target_link_libraries(scraps_web PRIVATE scraps_client_lib scraps_webview2 scraps_sdl3)
# Development builds find the UI in this build folder; release builds
# (SCRAPS_HOT_RELOAD off) carry no path of the build machine and look only
# next to the exe (or --ui-dir).
if(SCRAPS_HOT_RELOAD)
  target_compile_definitions(scraps_web PRIVATE "SCRAPS_WEB_UI_BUILD_DIR=\"${SCRAPS_UI_DIST}\"")
endif()
if(MSVC)
  target_link_options(scraps_web PRIVATE /ENTRY:mainCRTStartup)  # main(), not WinMain()
endif()
scraps_target_defaults(scraps_web)
if(SCRAPS_UI_TARGET)
  add_dependencies(scraps_web ${SCRAPS_UI_TARGET})
endif()
if(SCRAPS_WEBVIEW2_LOADER_DLL)
  # MinGW: the loader is loaded at run time from next to the exe.
  add_custom_command(TARGET scraps_web POST_BUILD
                     COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SCRAPS_WEBVIEW2_LOADER_DLL}"
                             "$<TARGET_FILE_DIR:scraps_web>/WebView2Loader.dll")
endif()

# The release: scraps_web.exe, the built UI as ui/ next to it, and the
# loader DLL when it is loaded at run time (cmake/ScrapsPackage.cmake adds
# the runtime DLLs it needs).
install(TARGETS scraps_web RUNTIME DESTINATION .)
install(DIRECTORY "${SCRAPS_UI_DIST}/" DESTINATION ui OPTIONAL)
# The WebView2 SDK's licence (BSD-3-Clause): its loader is linked in (MSVC)
# or shipped (MinGW).
install(FILES "${SCRAPS_WEBVIEW2_LICENSE}" DESTINATION licenses RENAME WebView2.txt)
# React, react-dom and scheduler (MIT) are bundled into ui/; the build strips
# their notices, so the release carries the licence text here.
install(FILES "${PROJECT_SOURCE_DIR}/ui/licenses/React.txt" DESTINATION licenses)
if(SCRAPS_WEBVIEW2_LOADER_DLL)
  install(FILES "${SCRAPS_WEBVIEW2_LOADER_DLL}" DESTINATION .)
endif()

# End to end, in a real window (needs a desktop session): the page loads,
# gets the view, Play is pressed, the page draws 120 frames, and the canvas
# has the world on it. 77 = skipped (no WebView2 runtime, or no desktop to
# draw on). The WebView2 profile stays in this build folder. TIMEOUT is
# longer than WebView2's start (30 s) and the self-test (90 s) together.
set(scraps_web_test_dir "${CMAKE_CURRENT_BINARY_DIR}/web_test")
file(MAKE_DIRECTORY "${scraps_web_test_dir}")
add_test(NAME web_window_self_test
         COMMAND scraps_web --self-test --save-dir "${scraps_web_test_dir}/saves"
                 --user-data-dir "${scraps_web_test_dir}/webview2-profile"
                 --screenshot "${scraps_web_test_dir}/self-test.png")
set_tests_properties(web_window_self_test PROPERTIES
                     LABELS window SKIP_RETURN_CODE 77 TIMEOUT 180
                     FAIL_REGULAR_EXPRESSION "FAILED|timed out")
