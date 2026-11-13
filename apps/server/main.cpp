// scraps_server: the dedicated server, run by the developer (Rule 5).
// Headless: it has no window, no renderer and no audio, only the simulation
// and a UDP socket.
//
// The game's code comes through a GameApi (core/game_api.h): the game this
// exe was built for (CMake's SCRAPS_GAME, through the generated
// selected_game.h), or with --hot-game that game's module next to it,
// swapped in again whenever it is rebuilt (common/game_module.h).

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>

#include "backends/dynlib/library.h"
#include "backends/udp/udp_net.h"
#include "common/game_module.h"
#include "common/random_secret.h"
#include "common/scene_dir.h"
#include "common/scene_reload.h"
#include "core/fixed_step.h"
#include "core/jobs.h"
#include "core/scene.h"
#include "net/server.h"
#include "selected_game.h"
#include "views/view_factory.h"

namespace {

struct Options {
  std::string scene = scraps_apps::selected_game().default_scene;
  std::string scene_dir;  // --scene-dir; main() fills it in otherwise (common/scene_dir.h)
  std::string bind = "0.0.0.0";
  long port = 7777;
  long max_players = 10;
  long snapshot_interval = 1;
  long ticks = 0;  // 0 = run until stopped
  long threads = -1;  // worker threads for simulate; -1 = one less than the core count
  bool hot_game = false;  // load the game module and reload it when it is rebuilt
  bool help = false;
};

void print_usage() {
  std::printf(
      "usage: scraps_server [options]           (game: %s)\n"
      "  --scene <name>            scene to host (default: %s)\n"
      "  --scene-dir <dir>         where .scene files are (default: scenes/ next to this\n"
      "                            program, else the game's source folder)\n"
      "  --port <n>                UDP port (default: 7777)\n"
      "  --bind <ip>               interface to listen on (default: 0.0.0.0 = all;\n"
      "                            127.0.0.1 = this machine only)\n"
      "  --max-players <n>         1 to 16 (default: 10)\n"
      "  --snapshot-interval <n>   ticks between snapshots (default: 1)\n"
      "  --ticks <n>               stop after n ticks (default: run until Ctrl+C)\n"
      "  --threads <n>             worker threads for the simulation (default: cores - 1; 0 = none)\n"
      "  --hot-game                run the game from its module and reload it when rebuilt\n"
      "                            (cmake --build --preset mingw-game / msvc-game;\n"
      "                            development builds only)\n"
      "Editing the .scene file while the server runs reloads it for everyone.\n",
      scraps_apps::selected_game().name, scraps_apps::selected_game().default_scene);
}

bool parse_long(const char* text, long min_value, long max_value, long& out) {
  char* end = nullptr;
  const long value = std::strtol(text, &end, 10);
  if (end == text || *end != '\0' || value < min_value || value > max_value) return false;
  out = value;
  return true;
}

bool parse_options(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--help" || arg == "-h") {
      options.help = true;
    } else if (arg == "--hot-game") {
      if (!scraps_apps::selected_game().hot_reload) {
        // Release builds link the game statically and ship no module.
        std::fprintf(stderr, "--hot-game is not available in this build\n");
        return false;
      }
      options.hot_game = true;
    } else if (!has_value) {
      std::fprintf(stderr, "unknown option or missing value: %s\n", arg.c_str());
      return false;
    } else if (arg == "--scene") {
      options.scene = argv[++i];
    } else if (arg == "--scene-dir") {
      options.scene_dir = argv[++i];
    } else if (arg == "--bind") {
      options.bind = argv[++i];
    } else if (arg == "--port") {
      if (!parse_long(argv[++i], 1, 65535, options.port)) return false;
    } else if (arg == "--max-players") {
      if (!parse_long(argv[++i], 1, scraps::kMaxPlayers, options.max_players)) return false;
    } else if (arg == "--snapshot-interval") {
      if (!parse_long(argv[++i], 1, 60, options.snapshot_interval)) return false;
    } else if (arg == "--ticks") {
      if (!parse_long(argv[++i], 1, 1000000000L, options.ticks)) return false;
    } else if (arg == "--threads") {
      if (!parse_long(argv[++i], 0, 64, options.threads)) return false;
    } else {
      std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace scraps;

  Options options;
  if (!parse_options(argc, argv, options)) {
    print_usage();
    return 2;
  }
  if (options.help) {
    print_usage();
    return 0;
  }

  const scraps_apps::SelectedGame& selected = scraps_apps::selected_game();
  const scraps_apps::SceneDirChoice scenes =
      scraps_apps::choose_scene_dir(options.scene_dir, dynlib::executable_directory(), selected.source_scene_dir);
  options.scene_dir = scenes.path;
  std::printf("game '%s', scenes from %s (%s)\n", selected.name, scenes.path.c_str(),
              scraps_apps::scene_dir_source_name(scenes.source));

  // The game's code. Declared before the Server, so it is destroyed after it:
  // a loaded module must outlive everything that calls into it.
  scraps_apps::GameModule game;
  std::string error;
  if (options.hot_game) {
    if (!game.load(selected.module_file, error)) {
      std::fprintf(stderr, "error: %s\nbuild the game module with: %s\n", error.c_str(), selected.build_command);
      return 1;
    }
    game.watch_other_builds(selected.other_builds);
    std::printf("game '%s' loaded from %s\nwatching %s; rebuild it with: %s\n", game.name(),
                game.live_path().c_str(), selected.module_file, selected.build_command);
  } else if (!game.use_static(scraps_apps::selected_game_api(), error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  Scene scene;
  if (!load_scene(options.scene_dir, options.scene, scene, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  Address bind_address;
  if (!parse_address(options.bind + ":0", bind_address)) {
    std::fprintf(stderr, "error: cannot parse --bind '%s'\n", options.bind.c_str());
    return 1;
  }
  std::unique_ptr<udp::UdpNet> net =
      udp::UdpNet::open(bind_address.ip, static_cast<std::uint16_t>(options.port), error);
  if (!net) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  JobSystem jobs(options.threads < 0 ? JobSystem::default_worker_count() : static_cast<unsigned>(options.threads));
  ServerConfig config;
  config.max_players = static_cast<int>(options.max_players);
  config.snapshot_interval_ticks = static_cast<int>(options.snapshot_interval);
  config.jobs = &jobs;
  config.cookie_secret = scraps_apps::random_cookie_secret();  // a new secret every run
  World world = make_world(scene, movement_rules_for(scene.level.perspective));
  Server server(*net, std::move(world), game.api().simulate, scene.name, scene.content_hash, config);
  scraps_apps::GameTargets game_targets;
  game_targets.server = &server;
  scraps_apps::SceneReloader reloader(options.scene_dir, options.scene);

  std::printf("Scraps server: scene '%s' (%s), listening on %s:%ld, up to %ld players, %u worker threads\n",
              scene.name.c_str(), perspective_name(scene.level.perspective), options.bind.c_str(), options.port,
              options.max_players, jobs.worker_count());
  std::printf("Clients join with: scraps_client --connect <this machine's ip>:%ld   (Ctrl+C stops the server)\n",
              options.port);
  std::fflush(stdout);

  FixedStepClock clock;
  const auto start = std::chrono::steady_clock::now();
  double last = 0.0;
  double next_status = 5.0;
  long ticks = 0;
  while (options.ticks == 0 || ticks < options.ticks) {
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::string message;
    if (reloader.poll(now, server, message)) {
      std::printf("%s\n", message.c_str());
      std::fflush(stdout);
    }
    // Hot game code: between ticks, a rebuilt module replaces the running
    // code. The World and every connection stay.
    if (game.hot() && game.poll(now, game_targets, message) != scraps_apps::GameReload::kNone) {
      std::printf("%s\n", message.c_str());
      std::fflush(stdout);
    }
    server.poll(now);
    const int steps = clock.advance(now - last);
    last = now;
    for (int i = 0; i < steps && (options.ticks == 0 || ticks < options.ticks); ++i) {
      server.tick();
      ++ticks;
    }
    if (now >= next_status) {
      std::printf("tick %u  clients %d  entities %lu\n", static_cast<unsigned>(server.world().tick),
                  server.client_count(), static_cast<unsigned long>(count_alive(server.world())));
      std::fflush(stdout);
      next_status += 5.0;
    }
    // Sleep a little so an idle server does not spin a core at 100%.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::printf("stopped after %ld ticks\n", ticks);
  return 0;
}
