#pragma once

// Game code hot reload (docs/specs/2026-11-12-mouse-and-hot-reload.md, part
// B), shared by scraps_server and the offline scraps_client, like
// scene_reload.h. Lives in apps/ because it wires a backend (dynlib, to load
// the module) to core (GameApi, FileWatcher), net (Server, Client) and views
// (Controls).
//
//   build writes scraps_sandbox_module.dll
//     -> FileWatcher sees it change (checked twice a second)
//     -> wait until size and time stay the same for one more check
//     -> copy it to scraps_sandbox_module.live-<n>.dll (Windows locks a
//        loaded DLL; the original must stay writable for the next build)
//     -> load the copy, ask scraps_game_api(), check version and layout
//     -> mismatch: unload it, keep the old code, say why
//     -> match: set_simulate on Server and Client, rebind Controls,
//        then unload the previous copy and delete its file.
//
// Paths are UTF-8 strings (what the build compiles in), converted with
// std::filesystem::u8path and back with u8string(), so a build folder with
// non-ASCII characters works on Windows too, and no conversion can throw.
//
// The GameModule object owns the loaded library (no globals). poll() must be
// called on the main thread between frames, when no tick and no job runs:
// JobSystem::parallel_for returns only when every job is done, so then no
// thread is inside the module's code.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "backends/dynlib/library.h"
#include "core/file_watch.h"
#include "core/game_api.h"
#include "net/client.h"
#include "net/server.h"
#include "views/controls.h"

namespace scraps_apps {

// Where a swap installs the game's code. Any of them may be null: a
// dedicated server has no Client and no Controls.
struct GameTargets {
  scraps::Server* server = nullptr;
  scraps::Client* client = nullptr;
  scraps::Controls* controls = nullptr;
};

enum class GameReload : std::uint8_t {
  kNone,      // nothing new (the usual case)
  kReloaded,  // the new code runs from the next tick
  kRefused,   // a new build was found but not used; the old code keeps running
  kHint,      // nothing changed here, but the developer should know something
};

// True if `text` is valid UTF-8 (std::filesystem::u8path may throw otherwise).
inline bool is_valid_utf8(const std::string& text) {
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    std::size_t extra = 0;
    if (lead < 0x80u) {
      extra = 0;
    } else if (lead >= 0xC2u && lead <= 0xDFu) {
      extra = 1;
    } else if (lead >= 0xE0u && lead <= 0xEFu) {
      extra = 2;
    } else if (lead >= 0xF0u && lead <= 0xF4u) {
      extra = 3;
    } else {
      return false;
    }
    if (extra > text.size() - i - 1) return false;
    for (std::size_t k = 1; k <= extra; ++k) {
      if ((static_cast<unsigned char>(text[i + k]) & 0xC0u) != 0x80u) return false;
    }
    i += extra + 1;
  }
  return true;
}

// True if `api` can run inside this program. Otherwise `error` says why.
// The version is read first and alone: if it differs, nothing else in the
// struct can be trusted to be where this program expects it.
inline bool check_game_api(const scraps::GameApi* api, std::string& error) {
  if (api == nullptr) {
    error = "the module gave no game description";
    return false;
  }
  if (api->api_version != scraps::kGameApiVersion) {
    error = "built against another engine (game API version " + std::to_string(api->api_version) +
            ", this program has " + std::to_string(scraps::kGameApiVersion) + ")";
    return false;
  }
  if (api->layout_hash != scraps::kGameLayoutHash) {
    error =
        "built against another engine (engine headers, core/jobs.cpp or core/world.cpp changed since this program "
        "was built, or another compiler); restart the program to use it";
    return false;
  }
  if (api->name == nullptr || api->name[0] == '\0') {
    error = "the game has no name";
    return false;
  }
  if (api->simulate == nullptr) {
    error = "the game has no simulate";
    return false;
  }
  if (api->binding_count > scraps::kMaxGameBindings || (api->binding_count > 0 && api->bindings == nullptr)) {
    error = "the game has too many input bindings";
    return false;
  }
  for (std::size_t i = 0; i < api->binding_count; ++i) {
    // Exactly one of key, mouse button and gamepad button, each in range.
    const scraps::GameBinding& binding = api->bindings[i];
    const int sources = (binding.key != scraps::Key::kUnknown ? 1 : 0) +
                        (binding.button != scraps::MouseButton::kCount ? 1 : 0) +
                        (binding.pad != scraps::GamepadButton::kCount ? 1 : 0);
    if (sources != 1 || binding.key >= scraps::Key::kCount || binding.button > scraps::MouseButton::kCount ||
        binding.pad > scraps::GamepadButton::kCount || binding.command == scraps::CommandType::kNone ||
        scraps::is_server_only(binding.command)) {
      error = "input binding " + std::to_string(i) + " of the game is not valid";
      return false;
    }
  }
  return true;
}

class GameModule {
 public:
  static constexpr double kCheckInterval = 0.5;  // seconds between looks at the file
  static constexpr int kMaxCopyTries = 6;        // checks a failing copy is retried before giving up
  static constexpr std::size_t kNameCapacity = 32;
  static constexpr std::size_t kMaxOtherBuilds = 4;
  static constexpr double kHintQuietSeconds = 10.0;  // at most one "other build" hint this often

  GameModule() = default;
  ~GameModule() { unload(m_library, m_live_path); }
  GameModule(const GameModule&) = delete;
  GameModule& operator=(const GameModule&) = delete;

  // The game linked into this program (`api` from e.g. sandbox::game_api()).
  // Nothing is watched; poll() never does anything.
  bool use_static(const scraps::GameApi* api, std::string& error) {
    if (!check_game_api(api, error)) return false;
    adopt(*api);
    return true;
  }

  // Loads the module at `path` (UTF-8) through a live copy, and watches
  // `path` for new builds. Live copies a previous run left behind (a server
  // stopped with Ctrl+C) are deleted first, unless another program still has
  // them loaded. Refused while a module is loaded: call it once.
  bool load(const std::string& path, std::string& error) {
    if (m_library) {
      error = "a game module is already loaded (" + m_live_path + ")";
      return false;
    }
    if (path.empty() || !is_valid_utf8(path)) {
      error = "the game module path is empty or not valid UTF-8";
      return false;
    }
    m_path = path;
    m_file = std::filesystem::u8path(path);
    m_watcher = scraps::FileWatcher(m_file);
    m_pending = false;
    std::error_code exists_error;
    if (!std::filesystem::exists(m_file, exists_error)) {
      error = "there is no game module at " + m_path;
      return false;
    }
    remove_stale_copies();

    std::unique_ptr<scraps::dynlib::Library> library;
    std::string live_path;
    const scraps::GameApi* api = nullptr;
    if (!copy_to_live(live_path, error)) {
      error = "cannot copy the game module: " + error;
      return false;
    }
    if (!open_and_check(live_path, library, api, error)) return false;
    adopt(*api);
    m_library = std::move(library);
    m_live_path = std::move(live_path);
    return true;
  }

  // Builds of the same module this program never loads (MSVC: the other
  // configurations' folders). If one of them changes while the watched file
  // does not, poll() says so (kHint): the developer probably built another
  // configuration than this program's. `list` holds UTF-8 paths separated by
  // '|'; empty entries are skipped.
  void watch_other_builds(const std::string& list) {
    m_other_build_count = 0;
    std::size_t begin = 0;
    while (begin <= list.size() && m_other_build_count < kMaxOtherBuilds) {
      std::size_t end = list.find('|', begin);
      if (end == std::string::npos) end = list.size();
      const std::string path = list.substr(begin, end - begin);
      if (!path.empty() && is_valid_utf8(path)) {
        m_other_builds[m_other_build_count++] = scraps::FileWatcher(std::filesystem::u8path(path));
      }
      begin = end + 1;
    }
  }

  // Puts the current code into `targets`: simulate into the Server and the
  // Client, the game's bindings into the Controls (replacing the old ones).
  void install(const GameTargets& targets) const {
    if (targets.server != nullptr) targets.server->set_simulate(m_api.simulate);
    if (targets.client != nullptr) targets.client->set_simulate(m_api.simulate);
    if (targets.controls != nullptr) {
      targets.controls->clear_bindings();
      for (std::size_t i = 0; i < m_binding_count; ++i) {
        const scraps::GameBinding& binding = m_bindings[i];
        if (binding.key != scraps::Key::kUnknown) {
          targets.controls->bind(binding.key, binding.command);
        } else if (binding.pad != scraps::GamepadButton::kCount) {
          targets.controls->bind(binding.pad, binding.command);
        } else {
          targets.controls->bind(binding.button, binding.command);
        }
      }
    }
  }

  // Call once per frame, between frames. Looks at the module file about
  // twice a second (allocating nothing unless a new build is there) and,
  // once a new build has stopped changing, swaps it in. `message` is set
  // when the result is not kNone.
  GameReload poll(double now, const GameTargets& targets, std::string& message) {
    if (!m_library || now < m_next_check) return GameReload::kNone;
    m_next_check = now + kCheckInterval;

    const bool changed = m_watcher.changed();
    FileStamp stamp;
    const bool has_file = read_stamp(stamp);
    if (!changed && !m_pending && other_build_changed(now)) {
      message = "game module not reloaded: " + m_hint_path + " was rebuilt, but this program reloads only " +
                m_path + "; build the module in this program's configuration";
      return GameReload::kHint;
    }
    if (changed) {
      // Something new, maybe still being written: look again next time.
      m_pending = has_file;
      m_pending_stamp = stamp;
      m_copy_failures = 0;
      return GameReload::kNone;
    }
    if (!m_pending) return GameReload::kNone;
    if (!has_file || !(stamp == m_pending_stamp) || stamp.size == 0) {
      // Still changing (a linker can grow a file within one time stamp).
      m_pending = has_file;
      m_pending_stamp = stamp;
      return GameReload::kNone;
    }
    // Unchanged for a whole check: the build is done.
    return reload(targets, message);
  }

  const scraps::GameApi& api() const { return m_api; }
  const char* name() const { return m_name.data(); }
  bool hot() const { return m_library != nullptr; }
  // The loaded copy (empty for a static game). For tests and messages.
  const std::string& live_path() const { return m_live_path; }
  unsigned reload_count() const { return m_reloads; }

  // The game's sound events and HUD text, safe to call whatever the game
  // provides. Presentation only.
  void sound_events(const scraps::World& before, const scraps::World& after, scraps::PlayerId player,
                    scraps::SoundEvents& out) const {
    out = scraps::SoundEvents();
    if (m_api.sound_events != nullptr) m_api.sound_events(before, after, player, out);
    if (out.count > scraps::SoundEvents::kMaxEvents) out.count = scraps::SoundEvents::kMaxEvents;
    for (std::size_t i = 0; i < out.count; ++i) out.names[i].back() = '\0';
  }
  void hud(const scraps::World& world, scraps::PlayerId player, char* out, std::size_t size) const {
    if (out == nullptr || size == 0) return;
    out[0] = '\0';
    if (m_api.hud != nullptr) m_api.hud(world, player, out, size);
    out[size - 1] = '\0';
  }

 private:
  struct FileStamp {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type time{};
    bool operator==(const FileStamp& other) const { return size == other.size && time == other.time; }
  };

  // No allocation: m_file was converted once, and the error_code overloads
  // never throw.
  bool read_stamp(FileStamp& out) const {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(m_file, error);
    if (error) return false;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(m_file, error);
    if (error) return false;
    out.size = size;
    out.time = time;
    return true;
  }

  // Copies what the module gave into this object: the name and bindings
  // live in the module's memory, which goes away at the next swap.
  void adopt(const scraps::GameApi& api) {
    m_api = api;
    m_name.fill('\0');
    for (std::size_t i = 0; i + 1 < kNameCapacity && api.name[i] != '\0'; ++i) m_name[i] = api.name[i];
    m_binding_count = api.binding_count;
    for (std::size_t i = 0; i < m_binding_count; ++i) m_bindings[i] = api.bindings[i];
    m_api.name = m_name.data();
    m_api.bindings = m_bindings.data();
  }

  // True when one of the other builds changed, at most once every
  // kHintQuietSeconds. Asks every watcher, so each change is seen once.
  bool other_build_changed(double now) {
    bool changed = false;
    for (std::size_t i = 0; i < m_other_build_count; ++i) {
      if (m_other_builds[i].changed() && !changed) {
        changed = true;
        m_hint_index = i;
      }
    }
    if (!changed || now < m_hint_quiet_until) return false;
    m_hint_quiet_until = now + kHintQuietSeconds;
    m_hint_path = m_other_builds[m_hint_index].path();
    return true;
  }

  bool same_bindings(const scraps::GameApi& api) const {
    if (api.binding_count != m_binding_count) return false;
    for (std::size_t i = 0; i < m_binding_count; ++i) {
      const scraps::GameBinding& a = api.bindings[i];
      const scraps::GameBinding& b = m_bindings[i];
      if (a.key != b.key || a.button != b.button || a.pad != b.pad || a.command != b.command) return false;
    }
    return true;
  }

  std::filesystem::path live_name(unsigned number) const {
    std::filesystem::path name = m_file.parent_path();
    name /= std::filesystem::u8path(m_file.stem().u8string() + ".live-" + std::to_string(number) +
                                    m_file.extension().u8string());
    return name;
  }

  void remove_stale_copies() const {
    const std::string prefix = m_file.stem().u8string() + ".live-";
    const std::string extension = m_file.extension().u8string();
    std::filesystem::path directory = m_file.parent_path();
    if (directory.empty()) directory = ".";
    std::error_code error;
    std::filesystem::directory_iterator it(directory, error);
    if (error) return;
    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(error)) {
      if (error) return;
      const std::string name = it->path().filename().u8string();
      if (name.compare(0, prefix.size(), prefix) != 0 || it->path().extension().u8string() != extension) continue;
      std::error_code ignored;
      std::filesystem::remove(it->path(), ignored);  // fails if another program has it loaded: fine
    }
  }

  // Copies the module to the first free live-<n> name. Never overwrites,
  // and never deletes a file this process did not create: a file with that
  // name may belong to another program (a server and a client watching the
  // same build).
  bool copy_to_live(std::string& live_path, std::string& error) {
    for (int attempt = 0; attempt < 64; ++attempt) {
      const std::filesystem::path target = live_name(m_next_number++);
      std::error_code exists_error;
      if (std::filesystem::exists(target, exists_error) || exists_error) continue;
      std::error_code copy_error;
      if (std::filesystem::copy_file(m_file, target, std::filesystem::copy_options::none, copy_error)) {
        live_path = target.u8string();
        return true;
      }
      // Another program took this number between the check and the copy:
      // the file is theirs. Leave it and try the next number.
      if (copy_error == std::errc::file_exists) continue;
      // Any other failure came after copy_options::none's "fail if it
      // exists" check, so a file left behind (a partial copy) is ours.
      std::error_code ignored;
      std::filesystem::remove(target, ignored);
      error = copy_error.message();
      return false;
    }
    error = "no free live-<n> file name next to " + m_path;
    return false;
  }

  // Loads `live_path` and checks what it describes. On failure the library
  // is unloaded again, its file deleted, and `error` says why.
  static bool open_and_check(std::string& live_path, std::unique_ptr<scraps::dynlib::Library>& library,
                             const scraps::GameApi*& api, std::string& error) {
    library = scraps::dynlib::Library::open(live_path, error);
    if (library) {
      const scraps::GameApiEntry entry = library->function<scraps::GameApiEntry>(scraps::kGameApiSymbol);
      if (entry == nullptr) {
        error = std::string("the module exports no ") + scraps::kGameApiSymbol + "()";
      } else {
        api = entry();
        if (check_game_api(api, error)) return true;
      }
    }
    unload(library, live_path);
    return false;
  }

  static void unload(std::unique_ptr<scraps::dynlib::Library>& library, std::string& live_path) {
    library.reset();  // unload first: Windows cannot delete a loaded DLL
    if (!live_path.empty()) {
      std::error_code ignored;
      std::filesystem::remove(std::filesystem::u8path(live_path), ignored);
      live_path.clear();
    }
  }

  GameReload reload(const GameTargets& targets, std::string& message) {
    std::string live_path;
    std::string error;
    if (!copy_to_live(live_path, error)) {
      // The linker may still hold the file; try again at the next check.
      if (++m_copy_failures < kMaxCopyTries) return GameReload::kNone;
      m_pending = false;
      message = "game module not reloaded: cannot copy " + m_path + ": " + error;
      return GameReload::kRefused;
    }
    // Changed while we copied it: the build was not done after all. Drop the
    // copy and wait for the file to settle again.
    FileStamp after_copy;
    if (!read_stamp(after_copy) || !(after_copy == m_pending_stamp)) {
      std::unique_ptr<scraps::dynlib::Library> none;
      unload(none, live_path);
      m_pending_stamp = after_copy;
      return GameReload::kNone;
    }
    m_pending = false;

    std::unique_ptr<scraps::dynlib::Library> library;
    const scraps::GameApi* api = nullptr;
    if (!open_and_check(live_path, library, api, error)) {
      message = "game module not reloaded: " + error;
      return GameReload::kRefused;
    }

    // The swap. Nothing runs game code right now (between frames).
    const bool rebind = !same_bindings(*api);
    adopt(*api);
    GameTargets swap = targets;
    if (!rebind) swap.controls = nullptr;  // same bindings: keep presses latched this frame
    install(swap);
    std::unique_ptr<scraps::dynlib::Library> previous = std::move(m_library);
    std::string previous_path = std::move(m_live_path);
    m_library = std::move(library);
    m_live_path = std::move(live_path);
    unload(previous, previous_path);
    ++m_reloads;
    message = std::string("game code reloaded (") + m_name.data() + ")";
    return GameReload::kReloaded;
  }

  // What runs now. m_api.name and m_api.bindings point into this object.
  scraps::GameApi m_api{};
  std::array<char, kNameCapacity> m_name{};
  std::array<scraps::GameBinding, scraps::kMaxGameBindings> m_bindings{};
  std::size_t m_binding_count = 0;

  // Hot only: the module the build writes, and the loaded copy of it.
  std::string m_path;
  std::filesystem::path m_file;
  scraps::FileWatcher m_watcher;
  std::unique_ptr<scraps::dynlib::Library> m_library;
  std::string m_live_path;
  unsigned m_next_number = 1;
  unsigned m_reloads = 0;
  double m_next_check = 0.0;
  bool m_pending = false;
  FileStamp m_pending_stamp;
  int m_copy_failures = 0;

  // Other configurations' builds of the module, for the "wrong build" hint.
  std::array<scraps::FileWatcher, kMaxOtherBuilds> m_other_builds{};
  std::size_t m_other_build_count = 0;
  std::size_t m_hint_index = 0;
  std::string m_hint_path;
  double m_hint_quiet_until = 0.0;
};

}  // namespace scraps_apps
