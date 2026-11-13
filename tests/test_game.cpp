// Game code as a module, and hot reload (spec 2026-11-12, part B).
//
// These tests load the real sandbox module (scraps_sandbox_module), two
// small modules built to be refused, and one built to be accepted whose code
// visibly differs (marker_game_module.cpp). Each test works on its own copy
// of a module in a folder under the build tree, so the module next to the
// apps is never touched and live copies never collide.
//
// Paths are UTF-8 strings, as in the apps (std::filesystem::u8path and
// u8string() to convert), so a test can use a non-ASCII folder.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "backends/headless/headless_backend.h"
#include "common/game_module.h"
#include "core/game_api.h"
#include "core/scene.h"
#include "core/serialize.h"
#include "core/jobs.h"
#include "harness.h"
#include "marker_game.h"
#include "net/client.h"
#include "net/loopback.h"
#include "net/server.h"
#include "sandbox/sandbox.h"
#include "views/controls.h"
#include "views/view_factory.h"

using namespace scraps;
namespace fs = std::filesystem;

namespace {

using scraps_apps::check_game_api;
using scraps_apps::GameModule;
using scraps_apps::GameReload;
using scraps_apps::GameTargets;

// ------------------------------------------------------------ files

fs::path u8(const std::string& path) { return fs::u8path(path); }

// An empty folder of the test's own under the build tree. `name` is UTF-8.
std::string fresh_dir(const char* name) {
  const fs::path dir = u8(SCRAPS_TEST_WORK_DIR) / u8(name);
  std::error_code error;
  fs::remove_all(dir, error);
  fs::create_directories(dir, error);
  return dir.u8string();
}

std::vector<char> read_file(const std::string& path) {
  std::ifstream in(u8(path), std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Writes `size` bytes of `data` to `path` and stamps it with `time`. Stamping
// explicitly makes "the file changed" independent of the clock's resolution.
bool write_file(const std::string& path, const std::vector<char>& data, std::size_t size, fs::file_time_type time) {
  {
    std::ofstream out(u8(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(size));
    if (!out) return false;
  }
  std::error_code error;
  fs::last_write_time(u8(path), time, error);
  return !error;
}

bool exists(const std::string& path) {
  std::error_code error;
  return fs::exists(u8(path), error);
}

fs::file_time_type module_time() {
  std::error_code error;
  return fs::last_write_time(u8(SCRAPS_SANDBOX_MODULE_FILE), error);
}

// A copy of the built sandbox module in its own folder: the file a test
// "rebuilds" by writing it again.
std::string watched_module(const char* test_name) {
  const std::string path = (u8(fresh_dir(test_name)) / u8(SCRAPS_SANDBOX_MODULE_FILE).filename()).u8string();
  const std::vector<char> bytes = read_file(SCRAPS_SANDBOX_MODULE_FILE);
  write_file(path, bytes, bytes.size(), module_time());
  return path;
}

// The live-<n> copy name the GameModule uses for `watched`.
std::string live_name(const std::string& watched, unsigned number) {
  const fs::path file = u8(watched);
  return (file.parent_path() / u8(file.stem().u8string() + ".live-" + std::to_string(number) +
                                  file.extension().u8string()))
      .u8string();
}

std::size_t live_copies(const std::string& watched) {
  const fs::path file = u8(watched);
  const std::string prefix = file.stem().u8string() + ".live-";
  std::size_t count = 0;
  std::error_code error;
  for (const fs::directory_entry& entry : fs::directory_iterator(file.parent_path(), error)) {
    if (entry.path().filename().u8string().compare(0, prefix.size(), prefix) == 0) ++count;
  }
  return count;
}

// "Rebuilds" `watched` with the bytes of `built` and lets the module see it:
// one check notices the change, the next (the file unchanged) swaps it in.
GameReload rebuild_and_poll(GameModule& module, const std::string& watched, const char* built, int seconds,
                            double now, const GameTargets& targets, std::string& message) {
  const std::vector<char> bytes = read_file(built);
  if (bytes.empty() || !write_file(watched, bytes, bytes.size(), module_time() + std::chrono::seconds(seconds))) {
    message = "cannot write " + watched;
    return GameReload::kNone;
  }
  const GameReload first = module.poll(now, targets, message);
  if (first != GameReload::kNone) return first;
  return module.poll(now + GameModule::kCheckInterval, targets, message);
}

bool load_module(test::Context& ctx, GameModule& module, const std::string& path) {
  std::string error;
  const bool ok = module.load(path, error);
  CHECK(ok);
  if (!ok) std::printf("    cannot load %s: %s\n", path.c_str(), error.c_str());
  return ok;
}

// ------------------------------------------------------------ worlds

World side_world() {
  Scene scene;
  std::string error;
  if (!load_scene(SCRAPS_SANDBOX_SCENE_DIR, "side", scene, error)) std::printf("    side scene: %s\n", error.c_str());
  return make_world(scene, movement_rules_for(scene.level.perspective));
}

std::vector<std::uint8_t> world_bytes(const World& world) {
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  write_world(writer, world);
  return bytes;
}

// The same 120 ticks of play every time: join, walk right then left, jump
// now and then, drop a coin and place one (so the module spawns entities).
void scripted_commands(const World& world, int i, CommandList& out) {
  out.clear();
  if (i == 0) {
    out.push(make_command(CommandType::kPlayerJoin, 0));
    return;
  }
  out.push(make_command(CommandType::kMove, 0, Vec3(i < 70 ? 1.0f : -1.0f, 0.0f, 0.0f)));
  if (i % 40 == 15) out.push(make_command(CommandType::kJump, 0));
  if (i == 20) out.push(make_command(sandbox::kDropCoin, 0));
  if (i == 45) {
    const Entity* me = find_entity(world, player_entity(world, 0));
    if (me != nullptr) out.push(make_command(sandbox::kPlaceCoin, 0, me->position + Vec3(1.2f, 0.0f, 0.0f)));
  }
}

// Runs script steps [from, to) with `simulate`. After the join, the player
// is handed three coins (test setup), so dropping and placing do something.
void run_script(World& world, SimulateFn simulate, int from, int to) {
  CommandList commands;
  for (int i = from; i < to; ++i) {
    scripted_commands(world, i, commands);
    simulate(world, commands, world.tick + 1, nullptr);
    if (i == 0) {
      Entity* me = find_entity(world, player_entity(world, 0));
      if (me != nullptr) me->user[sandbox::kUserCoins] = 3;
    }
  }
}

// ------------------------------------------------------------ tests

void static_api_describes_the_sandbox(test::Context& ctx) {
  GameModule game;
  std::string error;
  CHECK(game.use_static(sandbox::game_api(), error));
  CHECK(!game.hot());
  CHECK(std::string(game.name()) == "sandbox");
  CHECK(game.api().simulate == &sandbox::simulate);
  CHECK(game.api().binding_count == 3);
  CHECK(game.api().bindings[0].key == Key::kE && game.api().bindings[0].command == sandbox::kDropCoin);
  CHECK(game.api().bindings[1].button == MouseButton::kLeft && game.api().bindings[1].command == sandbox::kPlaceCoin);
  CHECK(game.api().bindings[2].pad == GamepadButton::kWest && game.api().bindings[2].command == sandbox::kDropCoin);
  World world = side_world();
  char hud[16];
  game.hud(world, 0, hud, sizeof(hud));
  CHECK(std::string(hud) == "coins 0");
  // A static game has nothing to watch.
  std::string message;
  CHECK(game.poll(0.0, GameTargets(), message) == GameReload::kNone);
}

// GameApi version 2: a game binding may name a gamepad button, and the app
// puts it into Controls like a key binding.
void gamepad_bindings_are_checked_and_installed(test::Context& ctx) {
  auto api_with = [](const GameBinding* bindings, std::size_t count) {
    GameApi api = *sandbox::game_api();
    api.bindings = bindings;
    api.binding_count = count;
    return api;
  };
  std::string error;
  const GameBinding good[] = {pad_binding(GamepadButton::kWest, sandbox::kDropCoin),
                              pad_binding(GamepadButton::kStart, game_command(3))};
  GameApi api = api_with(good, 2);
  CHECK(check_game_api(&api, error));

  // Exactly one source per binding, each in range.
  GameBinding two_sources = key_binding(Key::kE, sandbox::kDropCoin);
  two_sources.pad = GamepadButton::kSouth;
  GameBinding no_source;
  no_source.command = sandbox::kDropCoin;
  GameBinding out_of_range = pad_binding(GamepadButton::kSouth, sandbox::kDropCoin);
  out_of_range.pad = static_cast<GamepadButton>(200);
  const GameBinding no_command = pad_binding(GamepadButton::kSouth, CommandType::kNone);
  const GameBinding server_only = pad_binding(GamepadButton::kSouth, CommandType::kPlayerJoin);
  for (const GameBinding& bad : {two_sources, no_source, out_of_range, no_command, server_only}) {
    api = api_with(&bad, 1);
    error.clear();
    CHECK(!check_game_api(&api, error));
    CHECK(error == "input binding 0 of the game is not valid");
  }

  // The sandbox's own: west drops a coin, through the static game's install.
  GameModule game;
  CHECK(game.use_static(sandbox::game_api(), error));
  Controls controls;
  GameTargets targets;
  targets.controls = &controls;
  game.install(targets);
  headless::HeadlessBackend backend;
  backend.input().connect_gamepad(true);
  backend.input().press(GamepadButton::kWest);
  backend.window().pump_events();
  controls.sample(backend.input());
  const std::unique_ptr<View> view = make_view(Perspective::kSide);
  CommandList out;
  controls.emit(*view, view->movement_rules(), 0, out);
  int drops = 0;
  for (const Command& command : out) drops += command.type == sandbox::kDropCoin ? 1 : 0;
  CHECK(drops == 1);
}

void sound_event_names_are_copies(test::Context& ctx) {
  SoundEvents events;
  char name[32] = "coin";
  events.add(name);
  name[0] = 'x';  // the caller's string changes (or its module is unloaded)
  CHECK(std::string(events.name(0)) == "coin");
  events.add("a-name-much-longer-than-fifteen");
  CHECK(std::string(events.name(1)) == "a-name-much-lon");  // 15 characters kept
  events.add("c");
  events.add("d");
  events.add("e");  // full: ignored
  CHECK(events.count == SoundEvents::kMaxEvents);
  CHECK(std::string(events.name(3)) == "d");
}

void module_matches_static(test::Context& ctx) {
  GameModule module;
  if (!load_module(ctx, module, watched_module("matches"))) return;
  CHECK(module.hot());
  CHECK(std::string(module.name()) == "sandbox");
  CHECK(module.api().layout_hash == kGameLayoutHash);
  // Really the module's own copy of the code, not the one linked in here.
  CHECK(module.api().simulate != &sandbox::simulate);

  World by_module = side_world();
  World by_static = by_module;
  run_script(by_module, module.api().simulate, 0, 120);
  run_script(by_static, &sandbox::simulate, 0, 120);
  CHECK(by_module.tick == 120);
  CHECK(world_bytes(by_module) == world_bytes(by_static));
  // The script did play: the player exists, and a coin was dropped and one placed.
  const Entity* me = find_entity(by_module, player_entity(by_module, 0));
  CHECK(me != nullptr && me->user[sandbox::kUserCoins] == 1);

  // The module's presentation functions match too.
  char hud[16];
  module.hud(by_module, 0, hud, sizeof(hud));
  CHECK(std::string(hud) == "coins 1");
}

// An offline session, as scraps_client runs it: a Server and a Client over a
// loopback hub, stepped in turn, one tick a frame, sharing one JobSystem with
// two worker threads (as the app does). With `latency`, the client predicts
// ticks the server has not confirmed yet.
struct Offline {
  explicit Offline(SimulateFn simulate, const char* scene_name = "side", double latency = 0.0)
      : server_net(hub, std::uint16_t{1}), client_net(hub, std::uint16_t{2}), jobs(2) {
    hub.set_latency(latency);
    Scene scene;
    std::string error;
    if (!load_scene(SCRAPS_SANDBOX_SCENE_DIR, scene_name, scene, error)) {
      std::printf("    %s scene: %s\n", scene_name, error.c_str());
    }
    ServerConfig config;
    config.cookie_secret = CookieSecret{0x0123456789ABCDEFull, 0x1122334455667788ull};
    config.jobs = &jobs;
    server = std::make_unique<Server>(server_net, make_world(scene, movement_rules_for(scene.level.perspective)),
                                      simulate, scene.name, scene.content_hash, config);
    ClientConfig client_config;
    client_config.scene_directory = SCRAPS_SANDBOX_SCENE_DIR;
    client_config.jobs = &jobs;
    client = std::make_unique<Client>(client_net, server_net.local_address(), simulate, client_config);
  }

  void frame(int i) {
    now += 1.0 / 60.0;
    hub.set_now(now);
    client->poll(now);
    CommandList commands;
    if (client->state() == ClientState::kConnected) {
      commands.push(make_command(CommandType::kMove, client->player_id(), Vec3(i < 70 ? 1.0f : -1.0f, 0.0f, 0.0f)));
      if (i % 40 == 15) commands.push(make_command(CommandType::kJump, client->player_id()));
    }
    client->tick(commands, now);
    server->poll(now);
    server->tick();
    client->poll(now);
  }

  LoopbackHub hub;
  LoopbackNet server_net;
  LoopbackNet client_net;
  JobSystem jobs;  // declared before the Server and Client, so it outlives them
  std::unique_ptr<Server> server;
  std::unique_ptr<Client> client;
  double now = 0.0;
};

void swap_keeps_the_world(test::Context& ctx) {
  // Declared first: the module outlives the Server and Client that call it.
  GameModule module;
  const std::string watched = watched_module("swap");
  if (!load_module(ctx, module, watched)) return;
  Offline swapped(module.api().simulate);
  Controls controls;
  GameTargets targets;
  targets.server = swapped.server.get();
  targets.client = swapped.client.get();
  targets.controls = &controls;
  module.install(targets);

  Offline reference(&sandbox::simulate);
  for (int i = 0; i < 60; ++i) {
    swapped.frame(i);
    reference.frame(i);
  }

  // A fresh build of the module appears; between two frames it is swapped in.
  const std::string first_copy = module.live_path();
  const std::vector<char> bytes = read_file(SCRAPS_SANDBOX_MODULE_FILE);
  CHECK(write_file(watched, bytes, bytes.size(), module_time() + std::chrono::seconds(10)));
  std::string message;
  CHECK(module.poll(swapped.now, targets, message) == GameReload::kNone);  // seen; waits one more check
  CHECK(module.poll(swapped.now + 0.5, targets, message) == GameReload::kReloaded);
  CHECK(message == "game code reloaded (sandbox)");
  CHECK(module.reload_count() == 1);
  CHECK(module.live_path() != first_copy);
  CHECK(!exists(first_copy));  // the old copy was unloaded and deleted
  CHECK(exists(module.live_path()));
  CHECK(live_copies(watched) == 1);

  for (int i = 60; i < 120; ++i) {
    swapped.frame(i);
    reference.frame(i);
  }
  CHECK(swapped.client->state() == ClientState::kConnected);
  CHECK(swapped.server->world().tick == reference.server->world().tick);
  CHECK(world_bytes(swapped.server->world()) == world_bytes(reference.server->world()));
  CHECK(world_bytes(swapped.client->current_world()) == world_bytes(reference.client->current_world()));
  const Entity* me = find_entity(swapped.server->world(), player_entity(swapped.server->world(), 0));
  CHECK(me != nullptr);
}

// The tick the marker module stamped on `entity` (0: never), or -1 if there
// is no such entity.
std::int32_t marker_of(const World& world, EntityId entity) {
  const Entity* found = find_entity(world, entity);
  return found != nullptr ? found->user[marker_game::kMarkerSlot] : -1;
}

// The world's bytes with the marker slot cleared everywhere: what is left
// must match a run of the plain sandbox exactly.
std::vector<std::uint8_t> bytes_without_marker(const World& world) {
  World copy = world;
  for (Entity& entity : copy.entities) entity.user[marker_game::kMarkerSlot] = 0;
  return world_bytes(copy);
}

void swap_runs_the_new_code(test::Context& ctx) {
  GameModule module;  // declared first: it outlives the Server and Client that call it
  const std::string watched = watched_module("new_code");
  if (!load_module(ctx, module, watched)) return;
  // The crowd scene (1,000 moving entities): movement really is split over
  // the two worker threads, through the module's copy of the job system
  // driving the exe's JobSystem. With 50 ms latency the client predicts
  // ticks the server has not confirmed, so the client's world shows what
  // the client's own simulate did, not only what the snapshots carried.
  Offline swapped(module.api().simulate, "crowd", 0.05);
  Offline reference(&sandbox::simulate, "crowd", 0.05);
  Controls controls;
  GameTargets targets;
  targets.server = swapped.server.get();
  targets.client = swapped.client.get();
  targets.controls = &controls;
  module.install(targets);
  CHECK(swapped.jobs.worker_count() == 2);

  for (int i = 0; i < 60; ++i) {
    swapped.frame(i);
    reference.frame(i);
  }
  CHECK(swapped.client->state() == ClientState::kConnected);
  CHECK(count_alive(swapped.server->world()) > 2 * 128);  // more than one movement job chunk
  const EntityId server_player = player_entity(swapped.server->world(), swapped.client->player_id());
  CHECK(find_entity(swapped.server->world(), server_player) != nullptr);
  // The sandbox module never writes the marker slot.
  CHECK(marker_of(swapped.server->world(), server_player) == 0);
  CHECK(marker_of(swapped.client->current_world(), swapped.client->local_entity()) == 0);

  // The marker module is "built" over the watched file and swapped in.
  std::string message;
  CHECK(rebuild_and_poll(module, watched, SCRAPS_MARKER_MODULE_FILE, 10, swapped.now, targets, message) ==
        GameReload::kReloaded);
  CHECK(message == std::string("game code reloaded (") + marker_game::kName + ")");
  CHECK(std::string(module.name()) == marker_game::kName);
  const Tick swap_tick = swapped.server->world().tick;

  for (int i = 60; i < 120; ++i) {
    swapped.frame(i);
    reference.frame(i);
  }
  CHECK(swapped.client->state() == ClientState::kConnected);

  // The server ran the new code: its player carries the last tick.
  const World& server_world = swapped.server->world();
  CHECK(server_world.tick > swap_tick);
  CHECK(marker_of(server_world, server_player) == static_cast<std::int32_t>(server_world.tick));
  // The client did too: it is ahead of the server's last confirmed state,
  // and its own prediction stamped its newest tick. Old code would leave
  // the value the last snapshot carried, an older tick.
  const World& client_world = swapped.client->current_world();
  CHECK(swapped.client->unacknowledged_inputs() > 0);
  CHECK(marker_of(client_world, swapped.client->local_entity()) == static_cast<std::int32_t>(client_world.tick));
  // Nothing else differs from the plain sandbox, threads or not.
  CHECK(bytes_without_marker(server_world) == world_bytes(reference.server->world()));
  CHECK(bytes_without_marker(client_world) == world_bytes(reference.client->current_world()));
  CHECK(marker_of(reference.server->world(), server_player) == 0);
}

// load() is called once; a second call is refused and changes nothing.
void load_twice_is_refused(test::Context& ctx) {
  GameModule module;
  const std::string watched = watched_module("twice");
  if (!load_module(ctx, module, watched)) return;
  const std::string first_copy = module.live_path();
  std::string error;
  CHECK(!module.load(watched, error));
  CHECK(error.find("already loaded") != std::string::npos);
  CHECK(module.live_path() == first_copy);
  CHECK(exists(first_copy));
  CHECK(live_copies(watched) == 1);

  // A missing module is refused with a clear message.
  GameModule missing;
  CHECK(!missing.load((u8(fresh_dir("missing")) / "scraps_nothing.dll").u8string(), error));
  CHECK(error.find("there is no game module at") == 0);
}

// A live copy someone else made (another program watching the same build)
// is never overwritten or deleted; the reload takes the next number.
void reload_skips_foreign_copies(test::Context& ctx) {
  const std::string watched = watched_module("foreign");
  const std::string foreign = live_name(watched, 2);
  const std::vector<char> foreign_bytes = {'n', 'o', 't', ' ', 'm', 'i', 'n', 'e'};
  {
    GameModule module;
    if (!load_module(ctx, module, watched)) return;
    CHECK(module.live_path() == live_name(watched, 1));
    // Another program copies the build to the next number while this one runs.
    CHECK(write_file(foreign, foreign_bytes, foreign_bytes.size(), module_time()));

    std::string message;
    CHECK(rebuild_and_poll(module, watched, SCRAPS_SANDBOX_MODULE_FILE, 10, 0.0, GameTargets(), message) ==
          GameReload::kReloaded);
    CHECK(module.live_path() == live_name(watched, 3));
    CHECK(read_file(foreign) == foreign_bytes);
    CHECK(!exists(live_name(watched, 1)));  // ours: unloaded and deleted
  }
  // Destroying the module deleted its own copy only.
  CHECK(!exists(live_name(watched, 3)));
  CHECK(read_file(foreign) == foreign_bytes);
}

// The build folder may have any name: UTF-8 paths reach LoadLibraryW and
// std::filesystem intact (on MSVC a narrow std::filesystem::path would use
// the ANSI code page instead).
void module_loads_from_a_non_ascii_folder(test::Context& ctx) {
  GameModule module;
  const std::string watched = watched_module("hot-\xc3\xbc-\xe6\xa8\xa1\xe5\x9d\x97");  // "hot-", u-umlaut, "-", two CJK characters
  CHECK(exists(watched));
  if (!load_module(ctx, module, watched)) return;
  CHECK(exists(module.live_path()));
  CHECK(module.live_path().find("\xc3\xbc") != std::string::npos);
  std::string message;
  CHECK(rebuild_and_poll(module, watched, SCRAPS_SANDBOX_MODULE_FILE, 10, 0.0, GameTargets(), message) ==
        GameReload::kReloaded);
  CHECK(live_copies(watched) == 1);
  World world = side_world();
  run_script(world, module.api().simulate, 0, 10);
  CHECK(world.tick == 10);
}

void mismatch_is_refused(test::Context& ctx) {
  // A bad module cannot be the first one either.
  {
    GameModule module;
    const std::string dir = fresh_dir("mismatch_first");
    const std::string bad = (u8(dir) / u8(SCRAPS_BAD_VERSION_MODULE_FILE).filename()).u8string();
    const std::vector<char> bytes = read_file(SCRAPS_BAD_VERSION_MODULE_FILE);
    CHECK(write_file(bad, bytes, bytes.size(), module_time()));
    std::string error;
    CHECK(!module.load(bad, error));
    CHECK(error.find("built against another engine") != std::string::npos);
    CHECK(live_copies(bad) == 0);
  }

  GameModule module;
  const std::string watched = watched_module("mismatch");
  if (!load_module(ctx, module, watched)) return;
  const std::string good_copy = module.live_path();
  const SimulateFn good_simulate = module.api().simulate;
  int seconds = 10;
  double now = 0.0;
  for (const char* bad_file :
       {SCRAPS_BAD_VERSION_MODULE_FILE, SCRAPS_BAD_OLD_MODULE_FILE, SCRAPS_BAD_LAYOUT_MODULE_FILE}) {
    const std::vector<char> bytes = read_file(bad_file);
    seconds += 10;
    CHECK(write_file(watched, bytes, bytes.size(), module_time() + std::chrono::seconds(seconds)));
    std::string message;
    CHECK(module.poll(now, GameTargets(), message) == GameReload::kNone);
    CHECK(module.poll(now + 0.5, GameTargets(), message) == GameReload::kRefused);
    now += 1.0;
    CHECK(message.find("game module not reloaded: built against another engine") == 0);
    if (std::string(bad_file) == SCRAPS_BAD_OLD_MODULE_FILE) {
      // A module from before gamepad bindings says which version it has.
      CHECK(message.find("(game API version 1, this program has 2)") != std::string::npos);
    }
    // The old code is still the one running, still loaded from the same copy.
    CHECK(module.api().simulate == good_simulate);
    CHECK(std::string(module.name()) == "sandbox");
    CHECK(module.live_path() == good_copy);
    CHECK(exists(good_copy));
    CHECK(live_copies(watched) == 1);  // the refused copy was unloaded and deleted
  }

  // ...and it still runs exactly like the static game.
  World by_module = side_world();
  World by_static = by_module;
  run_script(by_module, module.api().simulate, 0, 60);
  run_script(by_static, &sandbox::simulate, 0, 60);
  CHECK(world_bytes(by_module) == world_bytes(by_static));
}

void module_heap_is_shared(test::Context& ctx) {
  GameModule module;
  if (!load_module(ctx, module, watched_module("heap"))) return;
  const SimulateFn simulate = module.api().simulate;

  World world = side_world();  // its vectors were allocated here, in the exe
  CommandList commands;
  commands.push(make_command(CommandType::kPlayerJoin, 0));
  simulate(world, commands, world.tick + 1, nullptr);
  Entity* me = find_entity(world, player_entity(world, 0));
  CHECK(me != nullptr);
  if (me == nullptr) return;
  me->user[sandbox::kUserCoins] = 40;

  // No spare room: every coin the module spawns grows the exe's vector, so
  // the module frees blocks the exe allocated and allocates new ones.
  world.entities.shrink_to_fit();
  world.free_slots.clear();
  world.free_slots.shrink_to_fit();
  const std::size_t capacity_before = world.entities.capacity();
  const std::size_t alive_before = count_alive(world);
  commands.clear();
  commands.push(make_command(sandbox::kDropCoin, 0));
  for (int i = 0; i < 30; ++i) simulate(world, commands, world.tick + 1, nullptr);
  CHECK(world.entities.capacity() > capacity_before);
  CHECK(count_alive(world) == alive_before + 30);

  // Now the exe destroys those entities and frees the module's blocks.
  std::size_t destroyed = 0;
  for (std::uint32_t i = 0; i < world.entities.size(); ++i) {
    if (world.entities[i].alive && world.entities[i].kind == sandbox::kKindCoin) {
      destroyed += destroy_entity(world, entity_id_at(world, i)) ? 1 : 0;
    }
  }
  CHECK(destroyed >= 30);
  World copy = world;  // allocated by the exe...
  simulate(copy, commands, copy.tick + 1, nullptr);  // ...grown or reused by the module
  world = World();     // the module's block freed here
  copy.entities.clear();
  copy.entities.shrink_to_fit();
  CHECK(world.entities.empty() && copy.entities.capacity() == 0);
}

void reload_waits_for_a_stable_file(test::Context& ctx) {
  GameModule module;
  const std::string watched = watched_module("stable");
  if (!load_module(ctx, module, watched)) return;
  const std::string first_copy = module.live_path();
  const std::vector<char> bytes = read_file(SCRAPS_SANDBOX_MODULE_FILE);
  const fs::file_time_type stamp = module_time() + std::chrono::seconds(30);
  std::string message;

  // The linker has written half of the new module.
  CHECK(write_file(watched, bytes, bytes.size() / 2, stamp));
  CHECK(module.poll(100.0, GameTargets(), message) == GameReload::kNone);
  CHECK(module.poll(100.2, GameTargets(), message) == GameReload::kNone);  // too soon to look again
  // It writes the rest within the same time stamp: only the size tells.
  CHECK(write_file(watched, bytes, bytes.size(), stamp));
  CHECK(module.poll(100.5, GameTargets(), message) == GameReload::kNone);
  CHECK(module.live_path() == first_copy);  // nothing loaded while it changed
  CHECK(live_copies(watched) == 1);
  // Unchanged for a whole check: now it is loaded.
  CHECK(module.poll(101.0, GameTargets(), message) == GameReload::kReloaded);
  CHECK(module.live_path() != first_copy);
  CHECK(module.reload_count() == 1);
  // And nothing more happens while the file stays as it is.
  CHECK(module.poll(101.5, GameTargets(), message) == GameReload::kNone);
  CHECK(module.poll(102.0, GameTargets(), message) == GameReload::kNone);
  CHECK(module.reload_count() == 1);
}

}  // namespace

void register_game_tests(test::Registry& registry) {
  registry.add("game.static_api_describes_the_sandbox", static_api_describes_the_sandbox);
  registry.add("game.gamepad_bindings_are_checked_and_installed", gamepad_bindings_are_checked_and_installed);
  registry.add("game.sound_event_names_are_copies", sound_event_names_are_copies);
  registry.add("game.module_matches_static", module_matches_static);
  registry.add("game.swap_keeps_the_world", swap_keeps_the_world);
  registry.add("game.swap_runs_the_new_code", swap_runs_the_new_code);
  registry.add("game.load_twice_is_refused", load_twice_is_refused);
  registry.add("game.reload_skips_foreign_copies", reload_skips_foreign_copies);
  registry.add("game.module_loads_from_a_non_ascii_folder", module_loads_from_a_non_ascii_folder);
  registry.add("game.mismatch_is_refused", mismatch_is_refused);
  registry.add("game.module_heap_is_shared", module_heap_is_shared);
  registry.add("game.reload_waits_for_a_stable_file", reload_waits_for_a_stable_file);
}
