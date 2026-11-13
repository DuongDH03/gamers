#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

// File paths in the engine are UTF-8 strings: scene folders come from the
// executable's folder (dynlib::executable_directory()), --scene-dir, or a
// path compiled in, and any of them may hold characters outside ASCII (a
// user folder named in Vietnamese, say). A std::string handed straight to
// std::ifstream is read in the ANSI code page on Windows, so such a folder
// could not be opened. Everything in core that opens a file by name goes
// through here.
namespace scraps {

// Turns a UTF-8 path into a std::filesystem::path without throwing (unlike
// std::filesystem::u8path, which may throw on bad input). Returns false, and
// leaves `out` empty, when `utf8` is not valid UTF-8 or holds a NUL byte.
bool utf8_to_path(std::string_view utf8, std::filesystem::path& out);

// Opens `utf8_path` for binary reading. False when the path is not valid
// UTF-8 or the file cannot be opened.
bool open_file_for_reading(std::string_view utf8_path, std::ifstream& file);

}  // namespace scraps
