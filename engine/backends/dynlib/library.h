#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <type_traits>

namespace scraps::dynlib {

// A loaded shared library (a .dll on Windows, a .so elsewhere). Used by the
// apps to load a game module for hot reload (spec 2026-11-12, part B); core
// never sees it, so `windows.h` and `dlfcn.h` stay in this backend (Rule 3).
//
// The library is unloaded when this object is destroyed. Every function
// pointer taken from it dies with it: the owner must make sure nothing still
// calls into the library (no tick, no job running) before letting it go.
class Library {
  struct Passkey {
    explicit Passkey() = default;
  };

 public:
  // Loads the library at `path` (UTF-8). Returns nullptr and sets `error`
  // on failure.
  static std::unique_ptr<Library> open(const std::string& path, std::string& error);

  Library(Passkey, void* handle);
  ~Library();
  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;

  // The address of an exported symbol, or nullptr when there is none.
  void* symbol(const char* name) const;

  // The exported function `name` as a pointer of type `Fn`, or nullptr. The
  // caller vouches that the export really has this type.
  template <typename Fn>
  Fn function(const char* name) const {
    static_assert(std::is_pointer<Fn>::value && std::is_function<typename std::remove_pointer<Fn>::type>::value,
                  "Fn must be a function pointer type");
    static_assert(sizeof(Fn) == sizeof(void*), "function and data pointers differ in size");
    void* address = symbol(name);
    Fn fn = nullptr;
    // Object and function pointers do not convert into each other in
    // portable C++; copying the bits is what every platform loader means.
    if (address != nullptr) std::memcpy(&fn, &address, sizeof(fn));
    return fn;
  }

 private:
  void* m_handle;  // HMODULE on Windows, the dlopen handle elsewhere
};

// The folder that holds the running program's executable, as UTF-8 with
// '/' separators ("C:/Games/Scraps"), or "" if the system cannot say. An
// executable is a loaded module too, which is why this lives here: the apps
// look for an installed release's scenes/ next to it (spec 2026-11-14,
// part 5), and the OS call needs windows.h.
std::string executable_directory();

// The folder part of a file path, with '/' separators: "C:/Games/app.exe"
// gives "C:/Games". A root keeps its separator ("C:\app.exe" gives "C:/",
// "/app" gives "/"); a bare file name gives "". executable_directory() uses
// it; it touches no file.
std::string directory_of(const std::string& file_path);

}  // namespace scraps::dynlib
