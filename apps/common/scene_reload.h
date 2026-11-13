#pragma once

// Scene hot reload, shared by scraps_server and the offline scraps_client.
// Lives in apps/ because it wires three layers together: core (scene files),
// views (movement rules for a perspective) and net (the Server).

#include <string>
#include <utility>

#include "core/file_watch.h"
#include "core/scene.h"
#include "net/server.h"
#include "views/view_factory.h"

namespace scraps_apps {

struct SceneReloader {
  scraps::FileWatcher watcher;
  std::string scene_directory;
  std::string scene_name;
  double next_check = 0.0;

  SceneReloader(const std::string& directory, const std::string& name)
      : watcher(scene_path(directory, name)), scene_directory(directory), scene_name(name) {}

  static std::string scene_path(const std::string& directory, const std::string& name) {
    std::string path = directory;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    return path + name + ".scene";
  }

  // Checks the file about twice a second. When it changed, reloads it into
  // `server`. Returns true when something happened; `message` then says what
  // (including parse errors, in which case the running world is kept).
  //
  // Not while the server is paused: a reloaded world gets its players back
  // through joins, which only a tick applies (Rule 1), so a reload during a
  // pause would show a world without the player until resume. The change
  // is not lost: the watcher still reports it at the first check after the
  // game resumes, and until then the old world stays on screen.
  bool poll(double now, scraps::Server& server, std::string& message) {
    if (server.paused()) return false;
    if (now < next_check) return false;
    next_check = now + 0.5;
    if (!watcher.changed()) return false;

    scraps::Scene scene;
    std::string error;
    if (!scraps::load_scene(scene_directory, scene_name, scene, error)) {
      message = "scene not reloaded: " + error;
      return true;
    }
    scraps::World world = scraps::make_world(scene, scraps::movement_rules_for(scene.level.perspective));
    server.reload(std::move(world), scene.name, scene.content_hash);
    message = "scene '" + scene.name + "' reloaded";
    return true;
  }
};

}  // namespace scraps_apps
