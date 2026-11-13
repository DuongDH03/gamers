#pragma once

// Where the apps find the game's scenes (spec 2026-11-14, part 5). In order:
//   1. --scene-dir, when given: the player or developer said so;
//   2. scenes/ next to the executable: an installed or unzipped release;
//   3. the game's scenes folder in the source tree, compiled in: development
//      builds run straight from the build folder. Release builds
//      (SCRAPS_HOT_RELOAD off) compile in no source path at all, so a
//      shipped exe neither carries nor tries the developer's folders.
// Only the last existed before, which broke on any other PC.

#include <filesystem>
#include <string>
#include <system_error>

#include "common/game_module.h"  // is_valid_utf8

namespace scraps_apps {

// kNotFound: a release build (no source folder compiled in) with no scenes/
// next to it. The path is then where scenes/ should have been, so the
// "cannot open scene file" error names the place that was looked at.
enum class SceneDirSource { kCommandLine, kNextToExecutable, kSourceTree, kNotFound };

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
    case SceneDirSource::kNotFound:
      return "not found: no scenes/ next to the executable; give --scene-dir";
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
  std::string expected = "scenes";  // where an installed release keeps them
  if (!executable_dir.empty() && is_valid_utf8(executable_dir)) {
    const std::filesystem::path next_to_exe = std::filesystem::u8path(executable_dir) / "scenes";
    expected = next_to_exe.generic_u8string();
    std::error_code error;
    if (std::filesystem::is_directory(next_to_exe, error)) {
      choice.path = expected;
      choice.source = SceneDirSource::kNextToExecutable;
      return choice;
    }
  }
  if (source_tree_dir.empty()) {
    // A release build: no source folder is compiled in (cmake/ScrapsGame.cmake).
    choice.path = expected;
    choice.source = SceneDirSource::kNotFound;
    return choice;
  }
  choice.path = source_tree_dir;
  choice.source = SceneDirSource::kSourceTree;
  return choice;
}

}  // namespace scraps_apps
