# CTest: boundary_hash_tracks_sources. Run with
#   cmake -DSCRAPS_ROOT=<repo> -DGENERATED_HEADER=<build>/generated/core/boundary_hash.h
#         -DWORK_DIR=<scratch folder> -P TestBoundaryHash.cmake
#
# Checks that
#   1. the generated header holds the hash of the sources as they are now
#      (a stale header means an edit did not re-run configure);
#   2. the hash depends on contents, not on where the tree is;
#   3. an edit to a core header, a platform header, jobs.cpp or world.cpp,
#      even a reorder of two fields or a comment, changes it; so does a new
#      header in engine/core;
#   4. an edit to other engine code (movement.cpp) does not: that is code
#      the module carries its own copy of, and what hot reload is for.

foreach(var SCRAPS_ROOT GENERATED_HEADER WORK_DIR)
  if(NOT ${var})
    message(FATAL_ERROR "${var} is not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/ScrapsBoundaryHash.cmake")

set(failures "")

# 1. The generated header is current.
scraps_boundary_files(files "${SCRAPS_ROOT}")
scraps_boundary_hash(real "${SCRAPS_ROOT}" ${files})
file(READ "${GENERATED_HEADER}" header)
string(FIND "${header}" "0x${real}ull" found)
if(found EQUAL -1)
  list(APPEND failures "the generated header does not hold the current hash ${real}: configure did not re-run")
endif()
list(LENGTH files count)
if(count LESS 20)
  list(APPEND failures "only ${count} boundary files found")
endif()

# 2. A copy of the tree elsewhere hashes the same. Only the boundary files
# and movement.cpp are copied.
file(REMOVE_RECURSE "${WORK_DIR}")
foreach(file IN LISTS files)
  file(RELATIVE_PATH relative "${SCRAPS_ROOT}" "${file}")
  get_filename_component(dir "${WORK_DIR}/${relative}" DIRECTORY)
  file(COPY "${file}" DESTINATION "${dir}")
endforeach()
file(COPY "${SCRAPS_ROOT}/engine/core/movement.cpp" DESTINATION "${WORK_DIR}/engine/core")

function(hash_of_copy out)
  scraps_boundary_files(copy_files "${WORK_DIR}")
  scraps_boundary_hash(h "${WORK_DIR}" ${copy_files})
  set(${out} "${h}" PARENT_SCOPE)
endfunction()

hash_of_copy(copy)
if(NOT copy STREQUAL real)
  list(APPEND failures "a copy of the sources hashes ${copy}, not ${real}")
endif()

# 3. and 4. Edit one file, hash, put it back. The edit is written with LF
# line endings like the sources (file(WRITE) would write CRLF on Windows,
# itself a change), and the original is put back by a binary copy.
function(check_edit relative should_change what)
  set(path "${WORK_DIR}/${relative}")
  file(READ "${path}" original)
  set(edited "${original}")
  if(what STREQUAL "comment")
    string(APPEND edited "// one more comment\n")
  elseif(what STREQUAL "swap")
    # Reorder two members: same sizes, other offsets for each.
    string(REPLACE "  int width = 0;\n  int depth = 0;" "  int depth = 0;\n  int width = 0;" edited "${original}")
    if(edited STREQUAL original)
      list(APPEND failures "the swap test found no width/depth pair in ${relative}")
      set(failures "${failures}" PARENT_SCOPE)
      return()
    endif()
  endif()
  file(CONFIGURE OUTPUT "${path}" CONTENT "${edited}" @ONLY NEWLINE_STYLE UNIX)
  hash_of_copy(after)
  get_filename_component(dir "${path}" DIRECTORY)
  file(COPY "${SCRAPS_ROOT}/${relative}" DESTINATION "${dir}")
  hash_of_copy(restored)
  if(NOT restored STREQUAL real)
    list(APPEND failures "hash differs after restoring ${relative}")
  endif()
  if(should_change AND after STREQUAL real)
    list(APPEND failures "a ${what} edit of ${relative} did not change the hash")
  elseif(NOT should_change AND NOT after STREQUAL real)
    list(APPEND failures "a ${what} edit of ${relative} changed the hash")
  endif()
  set(failures "${failures}" PARENT_SCOPE)
endfunction()

check_edit(engine/core/level.h TRUE swap)
check_edit(engine/core/tick.h TRUE comment)
check_edit(engine/core/game_api.h TRUE comment)
check_edit(engine/platform/input.h TRUE comment)
check_edit(engine/core/jobs.cpp TRUE comment)
check_edit(engine/core/world.cpp TRUE comment)
check_edit(engine/core/movement.cpp FALSE comment)

# A new header joins the hash.
file(WRITE "${WORK_DIR}/engine/core/added.h" "#pragma once\n")
hash_of_copy(with_new)
if(with_new STREQUAL real)
  list(APPEND failures "a new header in engine/core did not change the hash")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")

if(failures)
  list(JOIN failures "\n  " text)
  message(FATAL_ERROR "boundary hash:\n  ${text}")
endif()
message(STATUS "Boundary hash ${real} over ${count} files: OK")
