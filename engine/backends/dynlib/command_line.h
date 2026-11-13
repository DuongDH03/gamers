#pragma once

#include <string>
#include <vector>

namespace scraps::dynlib {

// The program's command-line arguments as UTF-8, argv[0] included. On
// Windows, `argv` from main() is in the ANSI code page, so a path such as
// --load "C:/Users/<a Vietnamese name>/x.sav" arrives mangled (characters
// outside the code page become '?'); this reads the wide command line
// instead (GetCommandLineW and CommandLineToArgvW, hence windows.h, hence
// this backend). Elsewhere, and if the wide command line cannot be read or
// does not split into `argc` arguments, it returns `argv` as given.
std::vector<std::string> utf8_arguments(int argc, char** argv);

}  // namespace scraps::dynlib
