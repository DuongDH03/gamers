# Runs one npm step in ui/ while holding a lock on ui/.npm.lock, so builds
# of several presets at once (mingw and msvc share ui/ and its
# node_modules/) never run `npm ci` under each other, or build or test
# while another one is replacing node_modules/.
#
#   cmake -DSTEP=ci    -DNPM=<npm>   -DUI_DIR=<ui> -DSTAMP=<file>  -P ScrapsNpm.cmake
#   cmake -DSTEP=build -DNPM=<npm>   -DUI_DIR=<ui> -DOUT_DIR=<dist> -P ScrapsNpm.cmake
#   cmake -DSTEP=test  -DNODE=<node> -DUI_DIR=<ui>                 -P ScrapsNpm.cmake
#
# `ci` skips the install when node_modules/ already matches the lock file
# (npm writes node_modules/.package-lock.json as it installs; a copy of
# package-lock.json made after a good install says which one it was), so the
# second preset does not throw away what the first one just installed.

foreach(var STEP UI_DIR)
  if(NOT ${var})
    message(FATAL_ERROR "${var} is not set")
  endif()
endforeach()

# Waits for the other preset's step; 15 minutes is far longer than any.
file(LOCK "${UI_DIR}/.npm.lock" GUARD PROCESS TIMEOUT 900 RESULT_VARIABLE locked)
if(NOT locked EQUAL 0)
  message(FATAL_ERROR "cannot lock ${UI_DIR}/.npm.lock: ${locked}")
endif()

if(STEP STREQUAL "ci")
  set(installed "${UI_DIR}/node_modules/.scraps-installed-lock.json")
  set(current FALSE)
  if(EXISTS "${installed}")
    file(SHA256 "${installed}" installed_hash)
    file(SHA256 "${UI_DIR}/package-lock.json" lock_hash)
    if(installed_hash STREQUAL lock_hash)
      set(current TRUE)
    endif()
  endif()
  if(current)
    message(STATUS "UI: node_modules already matches package-lock.json")
  else()
    execute_process(COMMAND "${NPM}" ci --no-audit --no-fund WORKING_DIRECTORY "${UI_DIR}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "npm ci failed (${result})")
    endif()
    file(COPY_FILE "${UI_DIR}/package-lock.json" "${installed}")
  endif()
  file(TOUCH "${STAMP}")
elseif(STEP STREQUAL "build")
  execute_process(COMMAND "${NPM}" run build -- --outDir "${OUT_DIR}" --emptyOutDir
                  WORKING_DIRECTORY "${UI_DIR}" RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "npm run build failed (${result})")
  endif()
elseif(STEP STREQUAL "test")
  execute_process(COMMAND "${NODE}" "${UI_DIR}/node_modules/vitest/vitest.mjs" run
                  WORKING_DIRECTORY "${UI_DIR}" RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "the UI tests failed (${result})")
  endif()
else()
  message(FATAL_ERROR "unknown STEP '${STEP}'")
endif()
