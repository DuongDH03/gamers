# Enforces the dependency direction from the constitution (section 3) by
# scanning #include lines. Run with:  cmake -DSCRAPS_ROOT=<repo> -P CheckLayers.cmake
#
# Each layer lists the include prefixes it may use. Standard headers (<...>)
# are allowed everywhere except third-party ones, which are listed per layer.

if(NOT SCRAPS_ROOT)
  message(FATAL_ERROR "SCRAPS_ROOT is not set")
endif()

set(violations "")

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
        # Third-party headers contain a '/' (SDL3/SDL.h); OS socket and
        # library-loading headers are named explicitly. Plain standard
        # headers have neither.
        set(third_party FALSE)
        if(path MATCHES "/" OR path MATCHES "^(winsock2|ws2tcpip|windows|unistd|fcntl|errno|dlfcn)\\.h$")
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
# Loading shared libraries (game modules for hot reload): the only place
# that may include windows.h or dlfcn.h.
check_layer("engine/backends/dynlib" "backends/dynlib/" "windows.h;dlfcn.h")
check_layer("games" "core/;platform/;views/;net/;sandbox/" "")

# Apps and tests may name backends, but load libraries only through
# backends/dynlib: windows.h and dlfcn.h appear nowhere else.
foreach(dir apps tests)
  file(GLOB_RECURSE files "${SCRAPS_ROOT}/${dir}/*.h" "${SCRAPS_ROOT}/${dir}/*.cpp")
  foreach(file IN LISTS files)
    file(STRINGS "${file}" lines REGEX "^[ \t]*#[ \t]*include[ \t]*<")
    foreach(line IN LISTS lines)
      string(TOLOWER "${line}" lower)  # <Windows.h> too
      if(lower MATCHES "#[ \t]*include[ \t]*<(windows|dlfcn)\\.h>")
        list(APPEND violations "${file}: ${line} (only engine/backends/dynlib may include it)")
      endif()
    endforeach()
  endforeach()
endforeach()

if(violations)
  list(JOIN violations "\n  " text)
  message(FATAL_ERROR "Layer rule violations (see constitution section 3):\n  ${text}")
endif()
message(STATUS "Layer rules: OK")
