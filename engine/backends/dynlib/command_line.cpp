#include "backends/dynlib/command_line.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <climits>
#include <cwchar>
#include <utility>

namespace scraps::dynlib {

namespace {

std::vector<std::string> as_given(int argc, char** argv) {
  std::vector<std::string> arguments;
  if (argv == nullptr) return arguments;
  for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i] != nullptr ? argv[i] : "");
  return arguments;
}

}  // namespace

std::vector<std::string> utf8_arguments(int argc, char** argv) {
#ifdef _WIN32
  int count = 0;
  LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
  if (wide == nullptr) return as_given(argc, argv);
  std::vector<std::string> arguments;
  bool ok = count == argc;
  for (int i = 0; ok && i < count; ++i) {
    const std::size_t length = std::wcslen(wide[i]);
    if (length > static_cast<std::size_t>(INT_MAX)) {
      ok = false;
      break;
    }
    const int wide_length = static_cast<int>(length);
    std::string text;
    if (wide_length > 0) {
      // WC_ERR_INVALID_CHARS: a broken argument (a lone surrogate) is not
      // turned into a different, valid-looking path.
      const int size =
          WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], wide_length, nullptr, 0, nullptr, nullptr);
      if (size <= 0) {
        ok = false;
        break;
      }
      text.assign(static_cast<std::size_t>(size), '\0');
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], wide_length, &text[0], size, nullptr, nullptr);
    }
    arguments.push_back(std::move(text));
  }
  LocalFree(wide);
  if (!ok) return as_given(argc, argv);
  return arguments;
#else
  return as_given(argc, argv);
#endif
}

}  // namespace scraps::dynlib
