// scraps_client: the player's executable.
//
// Offline (default): runs the real Server inside this process over an
// in-memory LoopbackHub (Rule 5: offline = client + embedded server).
// Online (--connect host:port): talks to a scraps_server over UDP.
//
// The game's code comes through a GameApi (core/game_api.h): the game this
// exe was built for (CMake's SCRAPS_GAME, through the generated
// selected_game.h), or with --hot-game (offline only) that game's module
// next to it, swapped in again whenever it is rebuilt (common/game_module.h).
//
// This file picks the concrete window backend (Rule 3): SDL3 for a window,
// or headless for automated runs, and runs the loop. What one frame does is
// in client_app.cpp (ClientApp::frame), so a host that owns its own window
// loop (a WebView) can drive the same client.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "backends/dynlib/library.h"
#include "backends/headless/headless_backend.h"
#if SCRAPS_HAS_SDL3
#include "backends/sdl3/sdl3_backend.h"
#endif
#include "client/client_app.h"
#include "common/scene_dir.h"
#include "platform/services.h"
#include "selected_game.h"
#include "views/view_factory.h"

namespace {

using namespace scraps;

struct Options {
  std::string scene = scraps_apps::selected_game().default_scene;
  std::string scene_dir;  // --scene-dir; parse_options() fills it in otherwise (common/scene_dir.h)
  std::string connect;  // empty: offline, with an embedded server
  std::string view;     // empty: the scene's own view
  bool headless = false;
  long frames = 0;        // stop after this many frames (0: never; headless default 600)
  bool autopilot = true;  // headless only: scripted key presses
  long threads = -1;      // worker threads for simulate; -1 = one less than the core count
  bool hot_game = false;  // load the game module and reload it when it is rebuilt
  bool help = false;
};

void print_usage() {
  std::printf(
      "usage: scraps_client [options]       (game: %s)\n"
      "  --scene <name>        scene to play offline (default: %s)\n"
      "  --scene-dir <dir>     where .scene files are (default: scenes/ next to this program,\n"
      "                        else the game's source folder)\n"
      "  --connect <ip:port>   join a scraps_server instead of playing offline\n"
      "  --view <name>         start in top-down, side or isometric view\n"
      "  --headless            no window: run --frames frames and print a summary\n"
      "  --frames <n>          quit after n frames (headless default: 600)\n"
      "  --no-autopilot        headless mode without scripted key presses\n"
      "  --threads <n>         worker threads for the simulation (default: cores - 1; 0 = none)\n"
      "  --hot-game            offline only: run the game from its module and reload it\n"
      "                        when rebuilt (cmake --build --preset mingw-game / msvc-game;\n"
      "                        development builds only)\n"
      "keys: WASD/arrows move, Space jump, E drop coin, left click place coin, V next view,\n"
      "      -/= or wheel zoom, Esc quit; offline: P pause, F5 quick save, F9 quick load\n"
      "gamepad: left stick/d-pad move, south (A on Xbox) jump, west (X on Xbox) drop coin,\n"
      "         start pause (offline)\n"
      "offline, editing the .scene file reloads it while the game runs\n",
      scraps_apps::selected_game().name, scraps_apps::selected_game().default_scene);
}

bool parse_options(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](std::string& out) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value after %s\n", arg.c_str());
        return false;
      }
      out = argv[++i];
      return true;
    };
    if (arg == "--scene") {
      if (!value(options.scene)) return false;
    } else if (arg == "--scene-dir") {
      if (!value(options.scene_dir)) return false;
    } else if (arg == "--connect") {
      if (!value(options.connect)) return false;
    } else if (arg == "--view") {
      if (!value(options.view)) return false;
      Perspective unused = Perspective::kTopDown;
      if (!parse_perspective(options.view, unused)) {
        std::fprintf(stderr, "unknown view '%s'\n", options.view.c_str());
        return false;
      }
    } else if (arg == "--frames") {
      std::string text;
      if (!value(text)) return false;
      options.frames = std::strtol(text.c_str(), nullptr, 10);
      if (options.frames <= 0) {
        std::fprintf(stderr, "--frames needs a positive number\n");
        return false;
      }
    } else if (arg == "--threads") {
      std::string text;
      if (!value(text)) return false;
      char* end = nullptr;
      options.threads = std::strtol(text.c_str(), &end, 10);
      if (end == text.c_str() || *end != '\0' || options.threads < 0 || options.threads > 64) {
        std::fprintf(stderr, "--threads needs a number from 0 to 64\n");
        return false;
      }
    } else if (arg == "--headless") {
      options.headless = true;
    } else if (arg == "--no-autopilot") {
      options.autopilot = false;
    } else if (arg == "--hot-game") {
      if (!scraps_apps::selected_game().hot_reload) {
        // Release builds link the game statically and ship no module.
        std::fprintf(stderr, "--hot-game is not available in this build\n");
        return false;
      }
      options.hot_game = true;
    } else if (arg == "--help" || arg == "-h") {
      options.help = true;
    } else {
      std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
      return false;
    }
  }
  if (options.hot_game && !options.connect.empty()) {
    // Online, the server's code is the authority; a client predicting with
    // other code only gets corrected, so it does not watch (spec, part B).
    std::fprintf(stderr, "--hot-game works offline only; run scraps_server --hot-game instead\n");
    return false;
  }
  const scraps_apps::SceneDirChoice scenes = scraps_apps::choose_scene_dir(
      options.scene_dir, scraps::dynlib::executable_directory(), scraps_apps::selected_game().source_scene_dir);
  options.scene_dir = scenes.path;
  if (!options.help) {
    std::printf("game '%s', scenes from %s (%s)\n", scraps_apps::selected_game().name, scenes.path.c_str(),
                scraps_apps::scene_dir_source_name(scenes.source));
  }
  return true;
}

// The loop: one ClientApp::frame per turn (client_app.h). It owns the clock
// ("only the loop reads the clock"): simulated 60 Hz frames for repeatable
// headless offline runs, real time otherwise.
int run(const Options& options, PlatformServices platform, headless::HeadlessInput* autopilot, bool fixed_frame_time) {
  scraps_apps::ClientOptions client_options;
  client_options.scene = options.scene;
  client_options.scene_dir = options.scene_dir;
  client_options.connect = options.connect;
  client_options.view = options.view;
  client_options.headless = options.headless;
  client_options.frames = options.frames;
  client_options.threads = options.threads;
  client_options.hot_game = options.hot_game;
  // On the heap: the app keeps fixed buffers for the UI bridge.
  const auto app = std::make_unique<scraps_apps::ClientApp>(client_options, platform, autopilot);
  std::string error;
  if (!app->start(error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  const auto start = std::chrono::steady_clock::now();
  double now = 0.0;
  while (true) {
    const double wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double frame_now = fixed_frame_time ? now + 1.0 / 60.0 : wall_seconds;
    if (!app->frame(frame_now, wall_seconds)) break;
    now = frame_now;
    // A headless client talking to a real server has no vsync to pace it;
    // sleep for about one 60 Hz frame so its clock matches the server's.
    if (options.headless && !fixed_frame_time) std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  return app->finish();
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    print_usage();
    return 2;
  }
  if (options.help) {
    print_usage();
    return 0;
  }

  if (options.headless) {
    if (options.frames == 0) options.frames = 600;
    scraps::headless::HeadlessBackend backend;
    // Offline, simulated time makes headless runs fast and repeatable. Online,
    // the server runs in real time, so the client must too.
    const bool fixed_frame_time = options.connect.empty();
    return run(options, backend.services(), options.autopilot ? &backend.input() : nullptr, fixed_frame_time);
  }

#if SCRAPS_HAS_SDL3
  scraps::WindowDesc desc;
  desc.title = "Scraps";
  std::string error;
  std::unique_ptr<scraps::sdl3::Sdl3Platform> platform = scraps::sdl3::Sdl3Platform::create(desc, error);
  if (!platform) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  return run(options, platform->services(), nullptr, false);
#else
  std::fprintf(stderr, "this client was built without SDL3; run it with --headless\n");
  return 1;
#endif
}
