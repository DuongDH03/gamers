#include "backends/dynlib/library.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <climits>
#include <vector>

namespace scraps::dynlib {

#ifdef _WIN32
namespace {

std::string last_error_text() {
  const DWORD code = GetLastError();
  char buffer[512] = {};
  const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                      buffer, static_cast<DWORD>(sizeof(buffer)), nullptr);
  std::string text(buffer, length);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.')) {
    text.pop_back();
  }
  return text + " (error " + std::to_string(static_cast<unsigned long>(code)) + ")";
}

}  // namespace
#endif

std::unique_ptr<Library> Library::open(const std::string& path, std::string& error) {
  if (path.empty()) {
    error = "no library path given";
    return nullptr;
  }
#ifdef _WIN32
  // Paths are UTF-8; LoadLibraryW takes UTF-16.
  if (path.size() > static_cast<std::size_t>(INT_MAX)) {
    error = "library path too long";
    return nullptr;
  }
  const int path_length = static_cast<int>(path.size());
  const int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), path_length, nullptr, 0);
  if (wide_length <= 0) {
    error = "library path is not valid UTF-8: " + path;
    return nullptr;
  }
  std::vector<wchar_t> wide(static_cast<std::size_t>(wide_length) + 1, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), path_length, wide.data(), wide_length);
  // No "missing DLL" message box: the app reports the error itself.
  const UINT old_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
  HMODULE handle = LoadLibraryW(wide.data());
  const std::string load_error = handle == nullptr ? last_error_text() : std::string();
  SetErrorMode(old_mode);
  if (handle == nullptr) {
    error = "cannot load " + path + ": " + load_error;
    return nullptr;
  }
  return std::make_unique<Library>(Passkey(), static_cast<void*>(handle));
#else
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* text = dlerror();
    error = "cannot load " + path + ": " + (text != nullptr ? text : "unknown error");
    return nullptr;
  }
  return std::make_unique<Library>(Passkey(), handle);
#endif
}

Library::Library(Passkey, void* handle) : m_handle(handle) {}

Library::~Library() {
  if (m_handle == nullptr) return;
#ifdef _WIN32
  FreeLibrary(static_cast<HMODULE>(m_handle));
#else
  dlclose(m_handle);
#endif
}

void* Library::symbol(const char* name) const {
  if (m_handle == nullptr || name == nullptr) return nullptr;
#ifdef _WIN32
  const FARPROC address = GetProcAddress(static_cast<HMODULE>(m_handle), name);
  void* out = nullptr;
  static_assert(sizeof(out) == sizeof(address), "function and data pointers differ in size");
  if (address != nullptr) std::memcpy(&out, &address, sizeof(out));
  return out;
#else
  return dlsym(m_handle, name);
#endif
}

}  // namespace scraps::dynlib
