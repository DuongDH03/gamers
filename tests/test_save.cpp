// Save and load (part 1) and pause (part 4) of
// docs/specs/2026-11-14-offline-essentials-and-ui.md.
//
//   save.*   the save format (core/save.h), and saving and loading an offline
//            game the way scraps_client does it (apps/common/save_game.h)
//   pause.*  pausing an offline game the way scraps_client does it
//            (apps/common/pause.h): no ticks, no timeouts, no burst after

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "common/pause.h"
#include "common/save_game.h"
#include "core/fixed_step.h"
#include "core/save.h"
#include "core/scene.h"
#include "core/serialize.h"
#include "harness.h"
#include "net/client.h"
#include "net/loopback.h"
#include "net/server.h"
#include "sandbox/sandbox.h"
#include "views/view_factory.h"

using namespace scraps;

namespace {

constexpr double kFrame = 1.0 / 60.0;

const char* game_name() { return sandbox::game_api()->name; }

Scene load_sandbox_scene(const char* name) {
  Scene scene;
  std::string error;
  if (!load_scene(SCRAPS_SANDBOX_SCENE_DIR, name, scene, error)) std::printf("    %s: %s\n", name, error.c_str());
  return scene;
}

World world_from(const Scene& scene) { return make_world(scene, movement_rules_for(scene.level.perspective)); }

std::vector<std::uint8_t> state_bytes(const World& world) {
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  write_world_state(writer, world);
  return bytes;
}

// A world that has been played a while: two players walked about, one
// dropped coins, the wanderers used the random state.
World played_world(const Scene& scene, int ticks) {
  World world = world_from(scene);
  for (int i = 0; i < ticks; ++i) {
    CommandList commands;
    if (i == 0) {
      commands.push(make_command(CommandType::kPlayerJoin, 0));
      commands.push(make_command(CommandType::kPlayerJoin, 1));
    }
    commands.push(make_command(CommandType::kMove, 0, Vec3(i < ticks / 2 ? 1.0f : -0.5f, 0.3f, 0.0f)));
    commands.push(make_command(CommandType::kMove, 1, Vec3(0.0f, -1.0f, 0.0f)));
    if (i % 50 == 25) commands.push(make_command(sandbox::kDropCoin, 0));
    sandbox::simulate(world, commands, world.tick + 1);
  }
  return world;
}

SaveTarget target_for(const Scene& scene) {
  SaveTarget target;
  target.game_name = game_name();
  target.scene_name = scene.name;
  target.scene_hash = scene.content_hash;
  return target;
}

// The checksum is the last four bytes: FNV-1a of everything before them.
// Recomputed here so a test can hand-edit a save and still get past it.
void resign(std::vector<std::uint8_t>& bytes) {
  std::uint32_t hash = 2166136261u;
  for (std::size_t i = 0; i + 4 < bytes.size(); ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  for (std::size_t i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = static_cast<std::uint8_t>(hash >> (8 * i));
}

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

float distance(const Vec3& a, const Vec3& b) {
  const Vec3 d = a - b;
  return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

std::string work_file(const char* name) {
  const std::filesystem::path dir = std::filesystem::u8path(SCRAPS_TEST_WORK_DIR) / "save";
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  const std::filesystem::path file = dir / name;
  std::filesystem::remove(file, error);
  return file.u8string();
}

// An offline session as scraps_client runs it: the real Server and a Client
// (or two) over a LoopbackHub, stepped in turn, through the app's pause.
struct Offline {
  explicit Offline(const char* scene_name = "top-down", double latency = 0.0) : server_net(hub, std::uint16_t{1}) {
    hub.set_latency(latency);
    const Scene scene = load_sandbox_scene(scene_name);
    ServerConfig config;
    config.cookie_secret = CookieSecret{0x0123456789ABCDEFull, 0x1122334455667788ull};
    server = std::make_unique<Server>(server_net, world_from(scene), &sandbox::simulate, scene.name,
                                      scene.content_hash, config);
    add_client();
  }

  Client& add_client() {
    nets.push_back(std::make_unique<LoopbackNet>(hub, static_cast<std::uint16_t>(2 + nets.size())));
    ClientConfig config;
    config.scene_directory = SCRAPS_SANDBOX_SCENE_DIR;
    clients.push_back(std::make_unique<Client>(*nets.back(), server_net.local_address(), &sandbox::simulate, config));
    return *clients.back();
  }
  Client& client() { return *clients.front(); }

  // One frame of the app's loop: poll, run the ticks the clock (and the
  // pause) allow, with `move` as the first client's input. Returns the ticks.
  int frame(Vec2 move = Vec2(), double frame_seconds = kFrame) {
    now += frame_seconds;
    hub.set_now(now);
    for (auto& c : clients) c->poll(now);
    const int steps = pause.advance(clock, frame_seconds);
    for (int s = 0; s < steps; ++s) {
      for (std::size_t i = 0; i < clients.size(); ++i) {
        CommandList commands;
        if (i == 0 && clients[i]->state() == ClientState::kConnected) {
          commands.push(make_command(CommandType::kMove, 0, Vec3(move.x, move.y, 0.0f)));
          if (play_ticks % 40 == 20) commands.push(make_command(sandbox::kDropCoin, 0));
        }
        clients[i]->tick(commands, now);
      }
      server->poll(now);
      server->tick();
      for (auto& c : clients) c->poll(now);
      ++play_ticks;
    }
    return steps;
  }
  void frames(int count, Vec2 move = Vec2()) {
    for (int i = 0; i < count; ++i) frame(move);
  }
  // Runs until the first client plays (has its entity), at most 2 s.
  bool join() {
    for (int i = 0; i < 120; ++i) {
      frame();
      if (find_entity(client().current_world(), client().local_entity()) != nullptr) return true;
    }
    return false;
  }

  LoopbackHub hub;
  LoopbackNet server_net;
  std::vector<std::unique_ptr<LoopbackNet>> nets;
  std::unique_ptr<Server> server;
  std::vector<std::unique_ptr<Client>> clients;
  FixedStepClock clock;
  scraps_apps::OfflinePause pause;
  double now = 0.0;
  long play_ticks = 0;
};

// ------------------------------------------------------------------ format

void round_trips_byte_for_byte(test::Context& ctx) {
  const Scene scene = load_sandbox_scene("top-down");
  const World played = played_world(scene, 300);
  CHECK(count_alive(played) > 2);

  std::vector<std::uint8_t> bytes;
  write_save(played, scene.name, scene.content_hash, game_name(), bytes);
  World loaded = world_from(scene);
  SaveHeader header;
  std::string error;
  CHECK(read_save(bytes.data(), bytes.size(), target_for(scene), loaded, header, error) == SaveLoad::kLoaded);
  CHECK(error.empty());
  CHECK(state_bytes(loaded) == state_bytes(played));  // the state, exactly
  CHECK(loaded.tick == played.tick);
  CHECK(loaded.rng_state == played.rng_state);
  CHECK(sandbox::coins_of(loaded, 0) == sandbox::coins_of(played, 0));

  // Saving the loaded world gives the same file again.
  std::vector<std::uint8_t> again;
  write_save(loaded, scene.name, scene.content_hash, game_name(), again);
  CHECK(again == bytes);

  // The loaded world plays on exactly like the original.
  World original = played;
  for (int i = 0; i < 120; ++i) {
    CommandList commands;
    commands.push(make_command(CommandType::kMove, 1, Vec3(1.0f, 0.0f, 0.0f)));
    sandbox::simulate(original, commands, original.tick + 1);
    sandbox::simulate(loaded, commands, loaded.tick + 1);
  }
  CHECK(state_bytes(loaded) == state_bytes(original));
}

void header_describes_the_save(test::Context& ctx) {
  const Scene scene = load_sandbox_scene("side");
  const World played = played_world(scene, 90);
  std::vector<std::uint8_t> bytes;
  write_save(played, scene.name, scene.content_hash, game_name(), bytes);
  CHECK(bytes.size() > 4 && bytes[0] == 'S' && bytes[1] == 'C' && bytes[2] == 'R' && bytes[3] == 'S');

  SaveHeader header;
  std::string error;
  CHECK(read_save_header(bytes.data(), bytes.size(), header, error));
  CHECK(header.version == kSaveFormatVersion);
  CHECK(header.game_name == "sandbox");
  CHECK(header.scene_name == "side");
  CHECK(header.scene_hash == scene.content_hash);
  CHECK(header.tick == played.tick);
}

void truncated_file_is_refused(test::Context& ctx) {
  const Scene scene = load_sandbox_scene("top-down");
  std::vector<std::uint8_t> bytes;
  write_save(played_world(scene, 120), scene.name, scene.content_hash, game_name(), bytes);
  const World untouched = world_from(scene);
  const std::vector<std::uint8_t> untouched_state = state_bytes(untouched);

  int refused = 0;
  int said_truncated = 0;
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    World world = untouched;
    SaveHeader header;
    std::string error;
    if (read_save(bytes.data(), size, target_for(scene), world, header, error) == SaveLoad::kRefused &&
        !error.empty() && state_bytes(world) == untouched_state) {
      ++refused;
    }
    if (size >= 6 && contains(error, "truncated")) ++said_truncated;  // past the magic and version
  }
  CHECK(refused == static_cast<int>(bytes.size()));
  CHECK(said_truncated == static_cast<int>(bytes.size()) - 6);

  World world = untouched;
  SaveHeader header;
  std::string error;
  CHECK(read_save(nullptr, 0, target_for(scene), world, header, error) == SaveLoad::kRefused);
  CHECK(contains(error, "empty"));
}

void corrupted_file_is_refused(test::Context& ctx) {
  const Scene scene = load_sandbox_scene("top-down");
  std::vector<std::uint8_t> bytes;
  write_save(played_world(scene, 120), scene.name, scene.content_hash, game_name(), bytes);
  const World untouched = world_from(scene);
  const std::vector<std::uint8_t> untouched_state = state_bytes(untouched);

  // Every single damaged byte is noticed, wherever it is.
  int refused = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    std::vector<std::uint8_t> damaged = bytes;
    damaged[i] = static_cast<std::uint8_t>(damaged[i] ^ (1u << (i % 8)));
    World world = untouched;
    SaveHeader header;
    std::string error;
    if (read_save(damaged.data(), damaged.size(), target_for(scene), world, header, error) == SaveLoad::kRefused &&
        !error.empty() && state_bytes(world) == untouched_state) {
      ++refused;
    }
  }
  CHECK(refused == static_cast<int>(bytes.size()));

  // Not a save at all.
  const std::string text = "scraps-scene 1\nview top-down\n";
  World world = untouched;
  SaveHeader header;
  std::string error;
  CHECK(read_save(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), target_for(scene), world, header,
                  error) == SaveLoad::kRefused);
  CHECK(contains(error, "not a Scraps save"));

  // Extra bytes at the end.
  std::vector<std::uint8_t> longer = bytes;
  longer.insert(longer.end() - 4, std::uint8_t{0});
  resign(longer);
  CHECK(read_save(longer.data(), longer.size(), target_for(scene), world, header, error) == SaveLoad::kRefused);
  CHECK(contains(error, "corrupted"));

  // A damaged world with a correct checksum (someone edited the file): the
  // state is still validated like network input. Slot 0's "alive" byte, a
  // bool, set to 7: the state starts after magic, version, the two names,
  // hash, tick and the state size; then tick u32, rng u64, slot count u32,
  // generation u32.
  std::vector<std::uint8_t> edited = bytes;
  const std::size_t state_at = 4 + 2 + 1 + std::string("sandbox").size() + 1 + scene.name.size() + 4 + 4 + 4;
  edited[state_at + 4 + 8 + 4 + 4] = 7;
  resign(edited);
  world = untouched;
  CHECK(read_save(edited.data(), edited.size(), target_for(scene), world, header, error) == SaveLoad::kRefused);
  CHECK(contains(error, "corrupted"));
  CHECK(state_bytes(world) == untouched_state);
}

void other_version_is_refused(test::Context& ctx) {
  const Scene scene = load_sandbox_scene("top-down");
  std::vector<std::uint8_t> bytes;
  write_save(played_world(scene, 30), scene.name, scene.content_hash, game_name(), bytes);
  bytes[4] = static_cast<std::uint8_t>(kSaveFormatVersion + 1);  // u16 after the magic
  resign(bytes);  // a real file of another version, not a damaged one

  World world = world_from(scene);
  SaveHeader header;
  std::string error;
  CHECK(read_save(bytes.data(), bytes.size(), target_for(scene), world, header, error) == SaveLoad::kRefused);
  CHECK(contains(error, "version 2"));
  CHECK(contains(error, "version 1"));
  CHECK(!read_save_header(bytes.data(), bytes.size(), header, error));
}

void other_scene_or_game_is_refused(test::Context& ctx) {
  const Scene top_down = load_sandbox_scene("top-down");
  const Scene side = load_sandbox_scene("side");
  std::vector<std::uint8_t> bytes;
  write_save(played_world(top_down, 30), top_down.name, top_down.content_hash, game_name(), bytes);

  World world = world_from(side);
  const std::vector<std::uint8_t> untouched_state = state_bytes(world);
  SaveHeader header;
  std::string error;
  CHECK(read_save(bytes.data(), bytes.size(), target_for(side), world, header, error) == SaveLoad::kRefused);
  CHECK(contains(error, "'top-down'"));
  CHECK(contains(error, "'side'"));
  CHECK(state_bytes(world) == untouched_state);

  SaveTarget other_game = target_for(top_down);
  other_game.game_name = "another-game";
  world = world_from(top_down);
  CHECK(read_save(bytes.data(), bytes.size(), other_game, world, header, error) == SaveLoad::kRefused);
  CHECK(contains(error, "'sandbox'"));
  CHECK(contains(error, "'another-game'"));
}

void changed_scene_loads_with_a_warning(test::Context& ctx) {
  const Scene scene = load_sandbox_scene("top-down");
  const World played = played_world(scene, 60);
  std::vector<std::uint8_t> bytes;
  write_save(played, scene.name, scene.content_hash, game_name(), bytes);

  // The same scene, edited since the save (another content hash).
  SaveTarget edited = target_for(scene);
  edited.scene_hash ^= 0x5A5A5A5Au;
  World world = world_from(scene);
  SaveHeader header;
  std::string error;
  CHECK(read_save(bytes.data(), bytes.size(), edited, world, header, error) == SaveLoad::kSceneChanged);
  CHECK(state_bytes(world) == state_bytes(played));
  CHECK(header.scene_hash == scene.content_hash);
}

// ----------------------------------------------------------------- offline

void offline_save_play_load(test::Context& ctx) {
  Offline game;
  CHECK(game.join());
  game.frames(120, Vec2(1.0f, 0.2f));
  const std::string path = work_file("offline.sav");
  std::string error;
  CHECK(scraps_apps::save_game(*game.server, game_name(), path, error));
  const std::vector<std::uint8_t> saved = state_bytes(game.server->world());
  const Tick saved_tick = game.server->world().tick;
  const EntityId saved_player = player_entity(game.server->world(), 0);
  const Vec3 saved_position = find_entity(game.server->world(), saved_player)->position;

  // Play on: walk somewhere else, drop more coins.
  game.frames(150, Vec2(-1.0f, -0.6f));
  CHECK(game.server->world().tick > saved_tick + 100);
  CHECK(state_bytes(game.server->world()) != saved);

  SaveHeader header;
  CHECK(scraps_apps::load_game(*game.server, game_name(), SCRAPS_SANDBOX_SCENE_DIR, path, header, error) ==
        SaveLoad::kLoaded);
  CHECK(header.tick == saved_tick);
  // The World equals the saved one, tick and all.
  CHECK(state_bytes(game.server->world()) == saved);

  // The player keeps their saved entity, and the client's prediction starts
  // over from the loaded world: no blend from where it was before the load.
  game.frame();
  CHECK(game.client().state() == ClientState::kConnected);
  CHECK(game.server->client_count() == 1);
  CHECK(player_entity(game.server->world(), 0) == saved_player);
  CHECK(game.server->world().tick == saved_tick + 1);
  CHECK(game.client().current_world().tick == saved_tick + 1);
  CHECK(state_bytes(game.client().current_world()) == state_bytes(game.server->world()));
  CHECK(game.client().previous_world().tick == game.client().current_world().tick);
  const Entity* shown = find_entity(game.client().display_world(0.5f), game.client().local_entity());
  CHECK(shown != nullptr && distance(shown->position, saved_position) < 0.5f);

  // And plays on from there in step with the server.
  game.frames(60, Vec2(0.0f, 1.0f));
  CHECK(game.client().state() == ClientState::kConnected);
  CHECK(state_bytes(game.client().current_world()) == state_bytes(game.server->world()));
}

void prediction_restarts_after_a_load_over_latency(test::Context& ctx) {
  Offline game("side", 0.05);  // 50 ms each way: the client predicts ahead
  CHECK(game.join());
  game.frames(90, Vec2(1.0f, 0.0f));
  const std::string path = work_file("latency.sav");
  std::string error;
  CHECK(scraps_apps::save_game(*game.server, game_name(), path, error));
  const Tick saved_tick = game.server->world().tick;
  const Vec3 saved_position = find_entity(game.server->world(), player_entity(game.server->world(), 0))->position;
  game.frames(200, Vec2(-1.0f, 0.0f));

  SaveHeader header;
  CHECK(scraps_apps::load_game(*game.server, game_name(), SCRAPS_SANDBOX_SCENE_DIR, path, header, error) ==
        SaveLoad::kLoaded);
  game.frames(60);  // standing still: everything settles
  CHECK(game.client().state() == ClientState::kConnected);
  const World& server_world = game.server->world();
  const World& client_world = game.client().current_world();
  CHECK(server_world.tick == saved_tick + 60);
  CHECK(client_world.tick >= server_world.tick && client_world.tick <= server_world.tick + 10);
  const Entity* server_player = find_entity(server_world, player_entity(server_world, 0));
  const Entity* client_player = find_entity(client_world, game.client().local_entity());
  CHECK(server_player != nullptr && client_player != nullptr);
  if (server_player == nullptr || client_player == nullptr) return;
  CHECK(distance(client_player->position, server_player->position) < 0.01f);
  CHECK(distance(server_player->position, saved_position) < 1.0f);  // back where it was saved (side view: it may settle)
}

void load_removes_players_who_are_not_here(test::Context& ctx) {
  Offline game;
  Client& second = game.add_client();
  CHECK(game.join());
  game.frames(30);
  CHECK(game.server->client_count() == 2);
  CHECK(find_entity(game.server->world(), player_entity(game.server->world(), 1)) != nullptr);
  const std::string path = work_file("two.sav");
  std::string error;
  CHECK(scraps_apps::save_game(*game.server, game_name(), path, error));

  second.disconnect();
  game.frames(10);
  CHECK(game.server->client_count() == 1);

  SaveHeader header;
  CHECK(scraps_apps::load_game(*game.server, game_name(), SCRAPS_SANDBOX_SCENE_DIR, path, header, error) ==
        SaveLoad::kLoaded);
  CHECK(find_entity(game.server->world(), player_entity(game.server->world(), 1)) != nullptr);  // as saved...
  game.frame();
  // ...but nobody plays it now: gone at the next tick. Player 0 stays.
  CHECK(find_entity(game.server->world(), player_entity(game.server->world(), 1)) == nullptr);
  CHECK(find_entity(game.server->world(), player_entity(game.server->world(), 0)) != nullptr);
}

void a_refused_load_changes_nothing(test::Context& ctx) {
  Offline game;
  CHECK(game.join());
  game.frames(30);
  const std::vector<std::uint8_t> before = state_bytes(game.server->world());
  SaveHeader header;
  std::string error;
  CHECK(scraps_apps::load_game(*game.server, game_name(), SCRAPS_SANDBOX_SCENE_DIR, work_file("missing.sav"),
                               header, error) == SaveLoad::kRefused);
  CHECK(contains(error, "no save"));

  // A save of another scene, in a file.
  Offline other("side");
  CHECK(other.join());
  const std::string path = work_file("side.sav");
  CHECK(scraps_apps::save_game(*other.server, game_name(), path, error));
  CHECK(scraps_apps::load_game(*game.server, game_name(), SCRAPS_SANDBOX_SCENE_DIR, path, header, error) ==
        SaveLoad::kRefused);
  CHECK(contains(error, "'side'"));
  CHECK(state_bytes(game.server->world()) == before);
  game.frame();
  CHECK(game.client().state() == ClientState::kConnected);
}

// ------------------------------------------------------------------- pause

void ten_seconds_of_pause_give_no_ticks_and_no_disconnect(test::Context& ctx) {
  Offline game;
  CHECK(game.join());
  game.frames(60, Vec2(1.0f, 0.0f));
  const Tick tick = game.server->world().tick;
  const std::vector<std::uint8_t> world = state_bytes(game.server->world());
  const std::uint64_t snapshots = game.server->full_snapshots_sent() + game.server->delta_snapshots_sent();
  const std::uint32_t unacked = game.client().unacknowledged_inputs();

  CHECK(game.pause.set(true, game.server.get(), game.client(), game.clock));
  CHECK(game.server->paused() && game.client().paused());
  int ticks = 0;
  for (int i = 0; i < 600; ++i) {  // 10 simulated seconds, twice any timeout
    ticks += game.frame(Vec2(1.0f, 0.0f));
    // A host may keep polling the server while paused (the app polls it
    // only around ticks); a paused client's silence must not drop it.
    game.server->poll(game.now);
    // Even called directly, nothing ticks while paused.
    game.client().tick(CommandList(), game.now);
    game.server->tick();
  }
  CHECK(ticks == 0);
  CHECK(game.server->world().tick == tick);
  CHECK(state_bytes(game.server->world()) == world);
  CHECK(game.server->full_snapshots_sent() + game.server->delta_snapshots_sent() == snapshots);
  CHECK(game.client().unacknowledged_inputs() == unacked);  // no inputs recorded or sent
  CHECK(game.hub.in_flight() == 0);
  CHECK(game.client().state() == ClientState::kConnected);
  CHECK(game.server->client_count() == 1);

  // Resumed, it carries on with the next tick, and nobody times out later
  // either: the paused time never counts as silence.
  CHECK(game.pause.set(false, game.server.get(), game.client(), game.clock));
  game.frames(360, Vec2(1.0f, 0.0f));
  CHECK(game.server->world().tick == tick + 360);
  CHECK(game.client().state() == ClientState::kConnected);
  CHECK(game.server->client_count() == 1);
}

void a_paused_game_is_byte_identical_to_one_that_never_paused(test::Context& ctx) {
  Offline steady;
  Offline paused;
  CHECK(steady.join());
  CHECK(paused.join());
  // Inputs follow the play tick, not the frame, as a player's would.
  auto play = [](Offline& game, long until) {
    while (game.play_ticks < until) {
      const long t = game.play_ticks;
      game.frame(Vec2(t % 120 < 60 ? 1.0f : -1.0f, t % 90 < 45 ? 0.5f : -0.5f));
    }
  };
  const long start = steady.play_ticks;
  CHECK(paused.play_ticks == start);
  play(steady, start + 240);
  play(paused, start + 100);
  paused.pause.set(true, paused.server.get(), paused.client(), paused.clock);
  for (int i = 0; i < 300; ++i) paused.frame(Vec2(1.0f, 1.0f));
  paused.pause.set(false, paused.server.get(), paused.client(), paused.clock);
  play(paused, start + 240);

  CHECK(paused.server->world().tick == steady.server->world().tick);
  CHECK(state_bytes(paused.server->world()) == state_bytes(steady.server->world()));
  CHECK(state_bytes(paused.client().current_world()) == state_bytes(steady.client().current_world()));
}

void resuming_runs_no_burst_of_ticks(test::Context& ctx) {
  Offline game;
  CHECK(game.join());
  // Frames at 50 Hz, so the clock always holds some leftover time.
  for (int i = 0; i < 37; ++i) game.frame(Vec2(), 1.0 / 50.0);
  CHECK(game.clock.alpha() > 0.0f);
  const Tick tick = game.server->world().tick;

  game.pause.set(true, game.server.get(), game.client(), game.clock);
  int paused_ticks = 0;
  for (int i = 0; i < 500; ++i) paused_ticks += game.frame(Vec2(), 1.0 / 50.0);  // 10 s
  paused_ticks += game.frame(Vec2(), 3.0);  // and a long stall (a window dragged) while paused
  CHECK(paused_ticks == 0);
  CHECK(game.server->world().tick == tick);

  game.pause.set(false, game.server.get(), game.client(), game.clock);
  CHECK(game.clock.alpha() == 0.0f);  // the leftover is forgotten too
  const int first = game.frame(Vec2(), 1.0 / 50.0);
  CHECK(first <= 1);
  int most = first;
  int total = first;
  for (int i = 1; i < 50; ++i) {
    const int steps = game.frame(Vec2(), 1.0 / 50.0);
    most = steps > most ? steps : most;
    total += steps;
  }
  CHECK(most <= 2);  // 50 Hz frames at 60 ticks/s: one or two, never a catch-up
  CHECK(total >= 59 && total <= 60);  // one second of frames, one second of ticks
  CHECK(game.server->world().tick == tick + static_cast<Tick>(total));
  CHECK(game.client().state() == ClientState::kConnected);
}

void clock_reset_drops_the_leftover(test::Context& ctx) {
  FixedStepClock clock;
  CHECK(clock.advance(0.010) == 0);
  CHECK(clock.alpha() > 0.5f);
  clock.reset();
  CHECK(clock.alpha() == 0.0f);
  CHECK(clock.advance(0.010) == 0);  // the earlier 10 ms are gone
  CHECK(clock.advance(1.0 / 60.0) == 1);
}

void online_the_client_ignores_pause(test::Context& ctx) {
  Offline game;
  CHECK(game.join());
  const Tick tick = game.server->world().tick;
  // Online there is no embedded server: the app passes none.
  CHECK(!game.pause.set(true, nullptr, game.client(), game.clock));
  CHECK(!game.pause.toggle(nullptr, game.client(), game.clock));
  CHECK(!game.pause.paused());
  CHECK(!game.client().paused());
  CHECK(!game.server->paused());
  game.frames(30, Vec2(1.0f, 0.0f));
  CHECK(game.server->world().tick == tick + 30);
}

}  // namespace

void register_save_tests(test::Registry& registry) {
  registry.add("save.round_trips_byte_for_byte", round_trips_byte_for_byte);
  registry.add("save.header_describes_the_save", header_describes_the_save);
  registry.add("save.truncated_file_is_refused", truncated_file_is_refused);
  registry.add("save.corrupted_file_is_refused", corrupted_file_is_refused);
  registry.add("save.other_version_is_refused", other_version_is_refused);
  registry.add("save.other_scene_or_game_is_refused", other_scene_or_game_is_refused);
  registry.add("save.changed_scene_loads_with_a_warning", changed_scene_loads_with_a_warning);
  registry.add("save.offline_save_play_load", offline_save_play_load);
  registry.add("save.prediction_restarts_after_a_load_over_latency", prediction_restarts_after_a_load_over_latency);
  registry.add("save.load_removes_players_who_are_not_here", load_removes_players_who_are_not_here);
  registry.add("save.a_refused_load_changes_nothing", a_refused_load_changes_nothing);
  registry.add("pause.ten_seconds_give_no_ticks_and_no_disconnect", ten_seconds_of_pause_give_no_ticks_and_no_disconnect);
  registry.add("pause.byte_identical_to_a_run_that_never_paused",
               a_paused_game_is_byte_identical_to_one_that_never_paused);
  registry.add("pause.resuming_runs_no_burst_of_ticks", resuming_runs_no_burst_of_ticks);
  registry.add("pause.clock_reset_drops_the_leftover", clock_reset_drops_the_leftover);
  registry.add("pause.online_the_client_ignores_pause", online_the_client_ignores_pause);
}
