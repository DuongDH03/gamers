# Runs at install time (cmake --install, CPack), from ScrapsPackage.cmake's
# install(CODE): copies every DLL the executables load that Windows itself
# does not provide next to them, so the release runs on a clean PC.
#
# In:  SCRAPS_RUNTIME_EXECUTABLES  the built executables
#      SCRAPS_RUNTIME_DIRS         folders searched first (the compiler's bin)
#      SCRAPS_RUNTIME_TOOL         objdump (MinGW) or dumpbin (MSVC)
#      SCRAPS_RUNTIME_COMMAND      the tool's full path
#      CMAKE_INSTALL_PREFIX        set by the install script

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
if(SCRAPS_RUNTIME_TOOL)
  set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "${SCRAPS_RUNTIME_TOOL}")
endif()
if(SCRAPS_RUNTIME_COMMAND)
  set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${SCRAPS_RUNTIME_COMMAND}")
endif()

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES ${SCRAPS_RUNTIME_EXECUTABLES}
  DIRECTORIES ${SCRAPS_RUNTIME_DIRS}
  RESOLVED_DEPENDENCIES_VAR resolved
  UNRESOLVED_DEPENDENCIES_VAR unresolved
  CONFLICTING_DEPENDENCIES_PREFIX conflicting
  # The API sets are names, not files: Windows maps them to its own DLLs.
  PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
  # Windows' own DLLs (kernel32, ws2_32, the UCRT, ...) are on every PC; the
  # VC runtime found there comes from InstallRequiredSystemLibraries instead.
  POST_EXCLUDE_REGEXES "^[A-Za-z]:[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]")

if(unresolved)
  list(JOIN unresolved ", " names)
  message(FATAL_ERROR "Scraps install: cannot find these DLLs the executables need: ${names}")
endif()
foreach(name IN LISTS conflicting_FILENAMES)
  list(JOIN conflicting_${name} ", " places)
  message(FATAL_ERROR "Scraps install: ${name} found in more than one place (${places}); "
                      "put the compiler's bin folder first on PATH")
endforeach()

foreach(dll IN LISTS resolved)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}" TYPE SHARED_LIBRARY FILES "${dll}")
endforeach()
