#pragma once

// Saving and loading the game to a file, shared by scraps_server (--save,
// --load) and the offline scraps_client (F5, F9). Spec:
// docs/specs/2026-11-14-offline-essentials-and-ui.md, part 1.
//
// Lives in apps/ because it wires three layers together: core (the save
// format, scene files), views (movement rules for a perspective) and net
// (the Server, which holds the true World: Rule 5). Which folder the file is
// in is decided by each app, never by core.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/file_path.h"
#include "core/save.h"
#include "core/scene.h"
#include "net/server.h"
#include "views/view_factory.h"

namespace scraps_apps {

// Paths are UTF-8 (SDL's preferences folder may hold any character),
// converted without throwing: a path that is not valid UTF-8 is an error.
inline bool save_path(const std::string& path, std::filesystem::path& out, std::string& error) {
  if (path.empty() || !scraps::utf8_to_path(path, out)) {
    error = "not a usable file name: '" + path + "'";
    return false;
  }
  return true;
}

// Writes `bytes` to `path`, creating its folder. Writes a temporary file
// next to it first and then renames it over the old one, so a crash while
// saving never leaves a half-written save behind.
inline bool write_save_file(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
  std::filesystem::path target;
  if (!save_path(path, target, error)) return false;
  std::error_code fs_error;
  if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), fs_error);
  std::filesystem::path temporary = target;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
      error = "cannot write " + temporary.u8string();
      return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file) {
      error = "cannot write " + temporary.u8string();
      return false;
    }
  }
  std::filesystem::rename(temporary, target, fs_error);
  if (fs_error) {
    error = "cannot replace " + path + ": " + fs_error.message();
    std::filesystem::remove(temporary, fs_error);
    return false;
  }
  return true;
}

// Reads a whole save file. Files larger than any save are refused unread.
inline bool read_save_file(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error) {
  std::filesystem::path source;
  if (!save_path(path, source, error)) return false;
  std::error_code fs_error;
  const std::uintmax_t size = std::filesystem::file_size(source, fs_error);
  if (fs_error) {
    error = "no save at " + path;
    return false;
  }
  if (size > scraps::kMaxSaveSize) {
    error = path + " is too big to be a save";
    return false;
  }
  std::ifstream file(source, std::ios::binary);
  bytes.resize(static_cast<std::size_t>(size));
  if (!file || !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    error = "cannot read " + path;
    return false;
  }
  return true;
}

// A save or load error from the functions here, for a UI: the same reason
// with the save file's path (long, and it may show the player's user name)
// put in words. The full message still goes to the console.
inline std::string save_error_for_ui(std::string error, const std::string& path) {
  if (error.rfind("no save at ", 0) == 0) return "there is no save yet";
  if (error.rfind("not a usable file name", 0) == 0) return "the save folder's name cannot be used";
  const std::string scene = "cannot load the scene for this save";
  if (error.rfind(scene, 0) == 0) return scene;  // the rest names the scene file
  if (path.empty()) return error;
  for (const std::string& named : {path + ".tmp", path}) {
    for (std::size_t at = error.find(named); at != std::string::npos; at = error.find(named, at)) {
      error.replace(at, named.size(), "the save file");
      at += 13;  // past "the save file"
    }
  }
  return error;
}

// Saves the server's World (read between ticks) to `path`.
inline bool save_game(const scraps::Server& server, const std::string& game_name, const std::string& path,
                      std::string& error) {
  std::vector<std::uint8_t> bytes;
  scraps::write_save(server.world(), server.scene_name(), server.scene_hash(), game_name, bytes);
  return write_save_file(path, bytes, error);
}

// Loads `path` into the server, between ticks: builds the World from the
// server's scene file as it is now, lays the save over it, and hands it to
// Server::load_state. `header` describes the save. Returns kRefused with a
// message (the running game is untouched), kLoaded, or kSceneChanged (loaded,
// but the scene file is not the one the save was made with: warn).
inline scraps::SaveLoad load_game(scraps::Server& server, const std::string& game_name,
                                  const std::string& scene_directory, const std::string& path,
                                  scraps::SaveHeader& header, std::string& error) {
  std::vector<std::uint8_t> bytes;
  if (!read_save_file(path, bytes, error)) return scraps::SaveLoad::kRefused;
  scraps::Scene scene;
  if (!scraps::load_scene(scene_directory, server.scene_name(), scene, error)) {
    error = "cannot load the scene for this save: " + error;
    return scraps::SaveLoad::kRefused;
  }
  scraps::World world = scraps::make_world(scene, scraps::movement_rules_for(scene.level.perspective));
  scraps::SaveTarget target;
  target.game_name = game_name;
  target.scene_name = scene.name;
  target.scene_hash = scene.content_hash;
  const scraps::SaveLoad result = scraps::read_save(bytes.data(), bytes.size(), target, world, header, error);
  if (result == scraps::SaveLoad::kRefused) return result;
  server.load_state(std::move(world), scene.name, scene.content_hash);
  return result;
}

}  // namespace scraps_apps
