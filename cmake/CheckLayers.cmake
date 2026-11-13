# Enforces the dependency direction from the constitution (section 3) by
# scanning #include lines. Run with:  cmake -DSCRAPS_ROOT=<repo> -P CheckLayers.cmake
#
# Each layer lists the include prefixes it may use. Standard headers (<...>)
# are allowed everywhere except third-party ones, which are listed per layer.

if(NOT SCRAPS_ROOT)
  message(FATAL_ERROR "SCRAPS_ROOT is not set")
endif()

set(violations "")

# Operating-system and SDK headers that are not standard C or C++ (compared
# in lower case, without ".h"): sockets, library loading, Win32, COM and the
# WebView2 SDK. Each may appear only in the backend that is listed for it below.
set(os_headers
    # POSIX
    unistd fcntl errno dlfcn
    # Winsock
    winsock winsock2 ws2tcpip ws2def mswsock iphlpapi
    # Win32
    windows windef winbase winuser wingdi winnt winerror winreg libloaderapi processthreadsapi
    synchapi fileapi handleapi psapi dbghelp timeapi mmsystem dwmapi
    # COM and the shell
    objbase objidl ole2 oleauto olectl unknwn combaseapi wtypes propidl
    shlwapi shellapi shlobj shlobj_core knownfolders wrl
    # The WebView2 SDK
    webview2 webview2environmentoptions webview2experimental)
list(JOIN os_headers "|" os_header_pattern)
set(os_header_pattern "^(${os_header_pattern})\\.h$")

# check_layer(<dir> <allowed quoted prefixes> <allowed angle prefixes>)
function(check_layer dir allowed_quoted allowed_angle)
  file(GLOB_RECURSE files "${SCRAPS_ROOT}/${dir}/*.h" "${SCRAPS_ROOT}/${dir}/*.cpp")
  foreach(file IN LISTS files)
    file(STRINGS "${file}" lines REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS lines)
      if(line MATCHES "#[ \t]*include[ \t]*\"([^\"]+)\"")
        set(path "${CMAKE_MATCH_1}")
        set(ok FALSE)
        foreach(prefix IN LISTS allowed_quoted)
          string(FIND "${path}" "${prefix}" pos)
          if(pos EQUAL 0)
            set(ok TRUE)
          endif()
        endforeach()
        if(NOT ok)
          list(APPEND violations "${file}: ${line}")
        endif()
      elseif(line MATCHES "#[ \t]*include[ \t]*<([^>]+)>")
        # Compared in lower case: Windows file names are not case-sensitive,
        # so <Windows.h> and <WinSock2.h> name the same headers.
        string(TOLOWER "${CMAKE_MATCH_1}" path)
        # Third-party headers contain a '/' (SDL3/SDL.h, wrl/client.h); OS
        # and SDK headers are named in os_headers. Plain standard headers
        # have neither.
        set(third_party FALSE)
        if(path MATCHES "/" OR path MATCHES "${os_header_pattern}")
          set(third_party TRUE)
        endif()
        if(third_party)
          set(ok FALSE)
          foreach(prefix IN LISTS allowed_angle)
            string(TOLOWER "${prefix}" prefix)
            string(FIND "${path}" "${prefix}" pos)
            if(pos EQUAL 0)
              set(ok TRUE)
            endif()
          endforeach()
          if(NOT ok)
            list(APPEND violations "${file}: ${line}")
          endif()
        endif()
      endif()
    endforeach()
  endforeach()
  set(violations "${violations}" PARENT_SCOPE)
endfunction()

check_layer("engine/platform" "platform/" "")
check_layer("engine/core" "core/;platform/;third_party/" "")
check_layer("engine/net" "net/;core/;platform/" "")
check_layer("engine/views" "views/;core/;platform/" "")
check_layer("engine/backends/headless" "backends/headless/;platform/" "")
check_layer("engine/backends/sdl3" "backends/sdl3/;platform/" "SDL3/")
check_layer("engine/backends/udp" "backends/udp/;platform/"
            "winsock2.h;ws2tcpip.h;unistd.h;fcntl.h;errno.h;sys/;netinet/;arpa/")
# Loading shared libraries (game modules for hot reload) and the wide
# command line: windows.h, shellapi.h or dlfcn.h.
check_layer("engine/backends/dynlib" "backends/dynlib/" "windows.h;shellapi.h;dlfcn.h")
# The WebView2 window (UI layout A): windows.h, the COM and shell headers it
# uses and the WebView2 SDK, and nothing of another backend (its sound and
# gamepad come from the app).
check_layer("engine/backends/webview2" "backends/webview2/;platform/"
            "windows.h;objidl.h;shlwapi.h;shellapi.h;webview2.h;webview2environmentoptions.h")
check_layer("games" "core/;platform/;views/;net/;sandbox/" "")

# Apps and tests may name backends, but reach the OS only through them: no
# OS or SDK header from os_headers (windows.h, dlfcn.h, WebView2.h, ...)
# appears there.
foreach(dir apps tests)
  file(GLOB_RECURSE files "${SCRAPS_ROOT}/${dir}/*.h" "${SCRAPS_ROOT}/${dir}/*.cpp")
  foreach(file IN LISTS files)
    file(STRINGS "${file}" lines REGEX "^[ \t]*#[ \t]*include[ \t]*<")
    foreach(line IN LISTS lines)
      if(line MATCHES "#[ \t]*include[ \t]*<([^>]+)>")
        string(TOLOWER "${CMAKE_MATCH_1}" path)  # <Windows.h> too
        if(path MATCHES "${os_header_pattern}")
          list(APPEND violations "${file}: ${line} (only the backends in engine/backends may include it)")
        endif()
      endif()
    endforeach()
  endforeach()
endforeach()

if(violations)
  list(JOIN violations "\n  " text)
  message(FATAL_ERROR "Layer rule violations (see constitution section 3):\n  ${text}")
endif()
message(STATUS "Layer rules: OK")
