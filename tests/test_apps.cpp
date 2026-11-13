// What the apps decide before a game starts (spec 2026-11-14, part 5):
// where the scenes are, and where the running program is.

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "backends/dynlib/library.h"
#include "common/scene_dir.h"
#include "harness.h"

namespace fs = std::filesystem;

namespace {

using scraps_apps::choose_scene_dir;
using scraps_apps::SceneDirSource;

// An empty folder of the test's own under the build tree.
fs::path fresh_dir(const char* name) {
  const fs::path dir = fs::u8path(SCRAPS_TEST_WORK_DIR) / name;
  std::error_code error;
  fs::remove_all(dir, error);
  fs::create_directories(dir, error);
  return dir;
}

void command_line_scene_dir_wins(test::Context& ctx) {
  const fs::path exe_dir = fresh_dir("scene_dir_command_line");
  std::error_code error;
  fs::create_directories(exe_dir / "scenes", error);
  const auto choice = choose_scene_dir("my/scenes", exe_dir.u8string(), "source/scenes");
  CHECK(choice.path == "my/scenes");
  CHECK(choice.source == SceneDirSource::kCommandLine);
}

void scenes_next_to_the_executable_come_second(test::Context& ctx) {
  const fs::path exe_dir = fresh_dir("scene_dir_next_to_exe");
  std::error_code error;
  fs::create_directories(exe_dir / "scenes", error);
  const auto choice = choose_scene_dir("", exe_dir.u8string(), "source/scenes");
  CHECK(choice.source == SceneDirSource::kNextToExecutable);
  CHECK(choice.path == (exe_dir / "scenes").generic_u8string());
  CHECK(choice.path.find('\\') == std::string::npos);
}

void source_tree_is_the_fallback(test::Context& ctx) {
  const fs::path exe_dir = fresh_dir("scene_dir_fallback");  // no scenes/ in it
  auto choice = choose_scene_dir("", exe_dir.u8string(), "source/scenes");
  CHECK(choice.path == "source/scenes");
  CHECK(choice.source == SceneDirSource::kSourceTree);
  // A file called "scenes" is not a scenes folder.
  {
    std::error_code error;
    fs::create_directories(exe_dir, error);
    std::ofstream file(exe_dir / "scenes", std::ios::binary);
  }
  choice = choose_scene_dir("", exe_dir.u8string(), "source/scenes");
  CHECK(choice.source == SceneDirSource::kSourceTree);
  // An unknown executable folder (the OS could not say), or a broken one.
  choice = choose_scene_dir("", "", "source/scenes");
  CHECK(choice.source == SceneDirSource::kSourceTree);
  choice = choose_scene_dir("", std::string("bad\xff\xfe"), "source/scenes");
  CHECK(choice.source == SceneDirSource::kSourceTree);
}

void executable_directory_holds_this_program(test::Context& ctx) {
  const std::string dir = scraps::dynlib::executable_directory();
  CHECK(!dir.empty());
  CHECK(dir.find('\\') == std::string::npos);
#ifdef _WIN32
  const char* name = "scraps_tests.exe";
#else
  const char* name = "scraps_tests";
#endif
  std::error_code error;
  CHECK(fs::exists(fs::u8path(dir) / name, error));
}

// A release build compiles in no source folder (cmake/ScrapsGame.cmake):
// with no scenes/ next to the exe either, the choice says so and names the
// place it looked, so "cannot open scene file" errors point there.
void release_build_says_where_it_looked(test::Context& ctx) {
  const fs::path exe_dir = fresh_dir("scene_dir_release");  // no scenes/ in it
  auto choice = choose_scene_dir("", exe_dir.u8string(), "");
  CHECK(choice.source == SceneDirSource::kNotFound);
  CHECK(choice.path == (exe_dir / "scenes").generic_u8string());
  CHECK(std::string(scraps_apps::scene_dir_source_name(choice.source)).find("--scene-dir") != std::string::npos);
  choice = choose_scene_dir("", "", "");
  CHECK(choice.source == SceneDirSource::kNotFound);
  CHECK(choice.path == "scenes");
  // --scene-dir and scenes/ next to the exe still work as before.
  CHECK(choose_scene_dir("my/scenes", exe_dir.u8string(), "").source == SceneDirSource::kCommandLine);
  std::error_code error;
  fs::create_directories(exe_dir / "scenes", error);
  CHECK(choose_scene_dir("", exe_dir.u8string(), "").source == SceneDirSource::kNextToExecutable);
}

// An exe in a drive's root folder is in "C:/", not "C:" ("C:scenes" would be
// relative to that drive's current folder).
void directory_of_keeps_a_root(test::Context& ctx) {
  using scraps::dynlib::directory_of;
  CHECK(directory_of("C:\\app.exe") == "C:/");
  CHECK(directory_of("C:/app.exe") == "C:/");
  CHECK(directory_of("C:\\Games\\Scraps\\scraps_client.exe") == "C:/Games/Scraps");
  CHECK(directory_of("C:/Games//app.exe") == "C:/Games");
  CHECK(directory_of("\\\\server\\share\\app.exe") == "//server/share");
  CHECK(directory_of("/app") == "/");
  CHECK(directory_of("/usr/bin/app") == "/usr/bin");
  CHECK(directory_of("app.exe").empty());
  CHECK(directory_of("").empty());
  // The root case end to end: the scenes folder next to it is C:/scenes.
  CHECK((fs::u8path(directory_of("C:\\app.exe")) / "scenes").generic_u8string() == "C:/scenes");
}

}  // namespace

void register_apps_tests(test::Registry& registry) {
  registry.add("apps.command_line_scene_dir_wins", command_line_scene_dir_wins);
  registry.add("apps.scenes_next_to_the_executable_come_second", scenes_next_to_the_executable_come_second);
  registry.add("apps.source_tree_is_the_fallback", source_tree_is_the_fallback);
  registry.add("apps.executable_directory_holds_this_program", executable_directory_holds_this_program);
  registry.add("apps.release_build_says_where_it_looked", release_build_says_where_it_looked);
  registry.add("apps.directory_of_keeps_a_root", directory_of_keeps_a_root);
}
