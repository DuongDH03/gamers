#pragma once

// Where the apps find the game's scenes (spec 2026-11-14, part 5). In order:
//   1. --scene-dir, when given: the player or developer said so;
//   2. scenes/ next to the executable: an installed or unzipped release;
//   3. the game's scenes folder in the source tree, compiled in: development
//      builds run straight from the build folder.
// Only the last existed before, which broke on any other PC.

#include <filesystem>
#include <string>
#include <system_error>

#include "common/game_module.h"  // is_valid_utf8

namespace scraps_apps {

enum class SceneDirSource { kCommandLine, kNextToExecutable, kSourceTree };

struct SceneDirChoice {
  std::string path;  // UTF-8
  SceneDirSource source = SceneDirSource::kSourceTree;
};

inline const char* scene_dir_source_name(SceneDirSource source) {
  switch (source) {
    case SceneDirSource::kCommandLine:
      return "--scene-dir";
    case SceneDirSource::kNextToExecutable:
      return "next to the executable";
    case SceneDirSource::kSourceTree:
      return "source tree";
  }
  return "?";
}

// `from_command_line`: --scene-dir, or "" when not given. `executable_dir`:
// the folder holding the running exe (dynlib::executable_directory()), or ""
// when unknown. `source_tree_dir`: the folder compiled in. All UTF-8.
inline SceneDirChoice choose_scene_dir(const std::string& from_command_line, const std::string& executable_dir,
                                       const std::string& source_tree_dir) {
  SceneDirChoice choice;
  if (!from_command_line.empty()) {
    choice.path = from_command_line;
    choice.source = SceneDirSource::kCommandLine;
    return choice;
  }
  if (!executable_dir.empty() && is_valid_utf8(executable_dir)) {
    const std::filesystem::path next_to_exe = std::filesystem::u8path(executable_dir) / "scenes";
    std::error_code error;
    if (std::filesystem::is_directory(next_to_exe, error)) {
      choice.path = next_to_exe.generic_u8string();
      choice.source = SceneDirSource::kNextToExecutable;
      return choice;
    }
  }
  choice.path = source_tree_dir;
  choice.source = SceneDirSource::kSourceTree;
  return choice;
}

}  // namespace scraps_apps
