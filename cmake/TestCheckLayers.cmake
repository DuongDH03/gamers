# CTest: layer_rules_catch_mixed_case. Run with
#   cmake -DSCRAPS_ROOT=<repo> -DWORK_DIR=<scratch folder> -P TestCheckLayers.cmake
#
# Builds a tiny fake tree in WORK_DIR whose core and apps include
# <Windows.h> (capital W: the same file on Windows), and whose core, views
# and udp backend include Windows SDK and COM headers (<WebView2.h>,
# <objidl.h>, <shlwapi.h>, <shellapi.h>), runs CheckLayers.cmake on it, and
# expects every line to be reported. A clean fake tree, where the webview2,
# dynlib and udp backends include what they use, must pass.

foreach(var SCRAPS_ROOT WORK_DIR)
  if(NOT ${var})
    message(FATAL_ERROR "${var} is not set")
  endif()
endforeach()

function(run_check root out_result out_text)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DSCRAPS_ROOT=${root}" -P "${SCRAPS_ROOT}/cmake/CheckLayers.cmake"
                  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  set(${out_result} "${result}" PARENT_SCOPE)
  set(${out_text} "${output}${error}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(WRITE "${WORK_DIR}/engine/core/clean.h" "#pragma once\n#include <vector>\n#include \"core/clean.h\"\n")
file(WRITE "${WORK_DIR}/engine/backends/sdl3/ok.cpp" "#include <SDL3/SDL.h>\n")
file(WRITE "${WORK_DIR}/engine/backends/dynlib/ok.cpp" "#include <Windows.h>\n#include <shellapi.h>\n")
file(WRITE "${WORK_DIR}/engine/backends/webview2/ok.cpp"
     "#include <windows.h>\n#include <objidl.h>\n#include <shlwapi.h>\n#include <shellapi.h>\n#include <WebView2.h>\n")
file(WRITE "${WORK_DIR}/engine/backends/udp/ok.cpp" "#include <winsock2.h>\n#include <ws2tcpip.h>\n")
run_check("${WORK_DIR}" clean_result clean_text)

file(WRITE "${WORK_DIR}/engine/core/bad.h" "#pragma once\n#include <Windows.h>\n")
file(WRITE "${WORK_DIR}/apps/bad.cpp" "#include <WINDOWS.H>\n")
file(WRITE "${WORK_DIR}/engine/core/bad_webview.cpp" "#include <WebView2.h>\n")
file(WRITE "${WORK_DIR}/engine/views/bad_com.cpp" "#include <objidl.h>\n")
file(WRITE "${WORK_DIR}/engine/core/bad_shell.cpp" "#include <ShlWApi.h>\n")
file(WRITE "${WORK_DIR}/engine/backends/udp/bad_shell.cpp" "#include <shellapi.h>\n")
file(WRITE "${WORK_DIR}/tests/bad_webview.cpp" "#include <webview2.h>\n")
run_check("${WORK_DIR}" bad_result bad_text)
file(REMOVE_RECURSE "${WORK_DIR}")

set(failures "")
if(NOT clean_result EQUAL 0)
  list(APPEND failures "a clean tree failed:\n${clean_text}")
endif()
if(bad_result EQUAL 0)
  list(APPEND failures "Windows headers in core, views, udp, apps and tests were not reported")
endif()
foreach(name "engine/core/bad.h" "apps/bad.cpp" "engine/core/bad_webview.cpp" "engine/views/bad_com.cpp"
        "engine/core/bad_shell.cpp" "engine/backends/udp/bad_shell.cpp" "tests/bad_webview.cpp")
  string(FIND "${bad_text}" "${name}" pos)
  if(pos EQUAL -1)
    list(APPEND failures "${name} is not named in the report:\n${bad_text}")
  endif()
endforeach()

if(failures)
  list(JOIN failures "\n  " text)
  message(FATAL_ERROR "layer rules:\n  ${text}")
endif()
message(STATUS "Layer rules catch <Windows.h> and the Windows SDK headers: OK")
