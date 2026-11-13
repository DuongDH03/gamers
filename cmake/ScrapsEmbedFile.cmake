# Writes a file's bytes into a C++ header as a '\0'-terminated char array,
# so a program carries the file inside it (the webview2 backend's renderer.js).
# A byte array has no length limit (string literals do, on MSVC).
#
#   cmake -DINPUT=<file> -DOUTPUT=<header> -DNAME=<identifier> -P ScrapsEmbedFile.cmake

foreach(var INPUT OUTPUT NAME)
  if(NOT ${var})
    message(FATAL_ERROR "${var} is not set")
  endif()
endforeach()

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
# Two hex digits per byte -> "0x..,"; 16 bytes per line.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n    " bytes "${bytes}")
get_filename_component(source_name "${INPUT}" NAME)
file(WRITE "${OUTPUT}.tmp"
  "// Generated from ${source_name} by cmake/ScrapsEmbedFile.cmake. Do not edit.\n"
  "#pragma once\n\n"
  "// ${byte_count} bytes and a '\\0'.\n"
  "inline constexpr unsigned char ${NAME}[] = {\n    ${bytes}0x00};\n")
# Only touch the header when the text changed, so nothing rebuilds needlessly.
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
