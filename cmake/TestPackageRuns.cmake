# The `package_runs` test (spec 2026-11-14, part 5): install the release
# into an empty folder and run it from there, with PATH cut down to Windows'
# own folders, as on a PC that has never seen a compiler. A DLL missing from
# the install makes the programs fail to start, and so the test.
#
#   cmake -DBUILD_DIR=<build> -DCONFIG=<Release> -DWORK_DIR=<scratch> [-DEXPECT_WEB=ON]
#         [-DEXPECT_WEBVIEW2_LOADER=ON] -P TestPackageRuns.cmake
#
# With EXPECT_WEB, the release must also hold scraps_web.exe and the built UI
# (ui/index.html and its assets); with EXPECT_WEBVIEW2_LOADER (MinGW), the
# WebView2Loader.dll it loads.

foreach(var BUILD_DIR WORK_DIR)
  if(NOT ${var})
    message(FATAL_ERROR "${var} is not set")
  endif()
endforeach()

set(prefix "${WORK_DIR}/install")
set(elsewhere "${WORK_DIR}/elsewhere")  # the working folder: not the install
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${elsewhere}")

set(config_args "")
if(CONFIG)
  set(config_args --config "${CONFIG}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}" ${config_args}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "cmake --install failed (${result}):\n${output}")
endif()

foreach(file scraps_client.exe scraps_server.exe README.txt scenes)
  if(NOT EXISTS "${prefix}/${file}")
    message(FATAL_ERROR "the install has no ${file}")
  endif()
endforeach()
if(EXPECT_WEB)
  foreach(file scraps_web.exe ui/index.html)
    if(NOT EXISTS "${prefix}/${file}")
      message(FATAL_ERROR "the install has no ${file}")
    endif()
  endforeach()
  file(GLOB ui_scripts "${prefix}/ui/assets/*.js")
  if(NOT ui_scripts)
    message(FATAL_ERROR "the install's ui/ folder has no script")
  endif()
endif()
if(EXPECT_WEBVIEW2_LOADER AND NOT EXISTS "${prefix}/WebView2Loader.dll")
  message(FATAL_ERROR "the install has no WebView2Loader.dll")
endif()
file(GLOB scenes "${prefix}/scenes/*.scene")
if(NOT scenes)
  message(FATAL_ERROR "the install's scenes/ folder has no .scene file")
endif()
file(GLOB dlls RELATIVE "${prefix}" "${prefix}/*.dll")
file(GLOB modules "${prefix}/*_module*")
if(modules)
  message(FATAL_ERROR "the install has a hot-reload module: ${modules}")
endif()
message(STATUS "installed into ${prefix}; DLLs: ${dlls}")

# Windows' own folders only. Child processes inherit this.
set(windows "$ENV{SystemRoot}")
if(NOT windows)
  set(windows "C:/Windows")
endif()
file(TO_NATIVE_PATH "${windows}" windows)
set(ENV{PATH} "${windows}\\System32;${windows}")

# run_installed(<folder>): the client and the server installed in <folder>
# start, from another working folder, and take their scenes from it. Output
# is read as UTF-8, like the apps print it.
function(run_installed folder)
  execute_process(COMMAND "${folder}/scraps_client.exe" --headless --frames 120
                  WORKING_DIRECTORY "${elsewhere}"
                  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output
                  ENCODING UTF-8 TIMEOUT 120)
  message(STATUS "scraps_client in ${folder}:\n${output}")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "the client installed in ${folder} failed (${result}); a DLL may be missing from the install")
  endif()
  # It must have used the installed scenes, not the source folder compiled in.
  string(FIND "${output}" "scenes from ${folder}/scenes (next to the executable)" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "the client installed in ${folder} did not take its scenes from ${folder}/scenes")
  endif()

  # --port 0: any free port, so parallel test runs never collide.
  execute_process(COMMAND "${folder}/scraps_server.exe" --bind 127.0.0.1 --port 0 --ticks 30
                  WORKING_DIRECTORY "${elsewhere}"
                  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output
                  ENCODING UTF-8 TIMEOUT 60)
  message(STATUS "scraps_server in ${folder}:\n${output}")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "the server installed in ${folder} failed (${result}); a DLL may be missing from the install")
  endif()
  string(FIND "${output}" "scenes from ${folder}/scenes (next to the executable)" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "the server installed in ${folder} did not take its scenes from ${folder}/scenes")
  endif()
endfunction()

run_installed("${prefix}")

# The same install in a folder whose name is not ASCII (a Windows user
# name in Vietnamese, a CJK word, a space): the apps get that folder as
# UTF-8 and must still open every scene, image, sound and music file in it.
set(unicode "${WORK_DIR}/Phương 模块")
file(COPY "${prefix}/" DESTINATION "${unicode}")
run_installed("${unicode}")
