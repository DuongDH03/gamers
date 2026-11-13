// Checks the code rule "no allocation inside simulate or the render path once
// stage 5 is done". This executable replaces global operator new with a
// counting version, warms everything up, then requires that a few hundred
// ticks and frames allocate nothing.
//
// The counter is global state, which the code rules forbid in the engine;
// replacing operator new can only be done this way, and only in this test.

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <system_error>

#include "backends/headless/headless_backend.h"
#include "common/game_module.h"
#include "common/pause.h"
#include "core/fixed_step.h"
#include "core/jobs.h"
#include "core/mixer.h"
#include "core/scene.h"
#include "core/serialize.h"
#include "net/client.h"
#include "net/cookie.h"
#include "net/loopback.h"
#include "net/server.h"
#include "sandbox/sandbox.h"
#include "views/controls.h"
#include "views/sprite_bank.h"
#include "views/view_factory.h"

namespace {
std::atomic<long> g_allocations{0};
}

void* operator new(std::size_t size) {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(size != 0 ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main() {
  using namespace scraps;

  Scene scene;
  std::string error;
  if (!load_scene(SCRAPS_SANDBOX_SCENE_DIR, "crowd", scene, error)) {
    std::printf("FAILED: %s\n", error.c_str());
    return 1;
  }
  World world = make_world(scene, movement_rules_for(scene.level.perspective));
  World previous = world;
  CommandList commands;
  commands.push(make_command(CommandType::kPlayerJoin, 0));
  sandbox::simulate(world, commands, world.tick + 1);

  headless::HeadlessRenderer renderer(1280, 720);
  SpriteBank sprites;  // textured tiles and animated sprites, like the real client
  std::string errors;
  if (!sprites.load(world.level, SCRAPS_SANDBOX_SCENE_DIR, renderer, errors)) {
    std::printf("FAILED: %s\n", errors.c_str());
    return 1;
  }
  JobSystem jobs(3);  // and movement on worker threads
  std::unique_ptr<View> views[3] = {make_view(Perspective::kTopDown), make_view(Perspective::kSide),
                                    make_view(Perspective::kIsometric)};
  Camera camera;
  camera.zoom = 8.0f;  // zoomed out so every entity is always on screen
  camera.target = Vec3(32.0f, 24.0f, 1.0f);

  // Delta snapshots: the server keeps recent states and encodes against one
  // a few ticks old; the client rebuilds over its own copy and keeps that.
  std::array<WorldState, 4> server_kept{};
  std::array<WorldState, 4> client_kept{};
  for (std::size_t i = 0; i < server_kept.size(); ++i) {
    reserve_state(server_kept[i], world.entities.capacity());
    reserve_state(client_kept[i], world.entities.capacity());
    copy_state(world, server_kept[i]);
    copy_state(world, client_kept[i]);
  }
  World client = world;
  client.entities.reserve(world.entities.capacity());
  client.free_slots.reserve(world.free_slots.capacity());
  std::vector<std::uint8_t> delta;
  delta.reserve(1u << 20);
  long bad_deltas = 0;

  // Sound: a mixer with a playing voice, mixed every tick like the client does.
  Mixer mixer;
  SoundData beep;
  beep.samples.assign(static_cast<std::size_t>(kSoundSampleRate) * kSoundChannels, 0.1f);  // 1 s
  const SoundId beep_id = mixer.add(beep);
  std::vector<float> mixed(800 * kSoundChannels);

  // Mouse: a pointer that moves every frame and clicks now and then, sampled
  // and picked through every view like the client does.
  headless::HeadlessBackend input_backend;
  headless::HeadlessInput& input = input_backend.input();
  Controls controls;
  controls.bind(MouseButton::kLeft, sandbox::kPlaceCoin);
  CommandList picked;
  float pick_sum = 0.0f;  // used, so the picking is not optimised away

  // Hot game code: a loaded module whose file is looked at twice a second
  // (here about ten times in 300 ticks) while nothing changes, plus the
  // game's HUD and sound functions every frame, like the client does.
  // (Allocations inside the module itself are not counted here: it has its
  // own operator new. The sandbox's code is the same as the static one.)
  scraps_apps::GameModule game;
  {
    const std::filesystem::path dir = std::filesystem::u8path(SCRAPS_TEST_WORK_DIR) / "alloc";
    const std::filesystem::path module_file =
        dir / std::filesystem::u8path(SCRAPS_SANDBOX_MODULE_FILE).filename();
    std::error_code fs_error;
    std::filesystem::create_directories(dir, fs_error);
    // Removed first and checked: an old copy left in place (MinGW's
    // copy_file does not always honour overwrite_existing) would be a module
    // from an older build, which the layout hash rightly refuses.
    std::filesystem::remove(module_file, fs_error);
    if (!std::filesystem::copy_file(std::filesystem::u8path(SCRAPS_SANDBOX_MODULE_FILE), module_file,
                                    std::filesystem::copy_options::none, fs_error)) {
      std::printf("FAILED: cannot copy the game module: %s\n", fs_error.message().c_str());
      return 1;
    }
    if (!game.load(module_file.u8string(), error)) {
      std::printf("FAILED: %s\n", error.c_str());
      return 1;
    }
    // Other configurations' builds are looked at too (MSVC apps watch them
    // for the "wrong configuration" hint): one that exists, one that does not.
    game.watch_other_builds(std::filesystem::u8path(SCRAPS_SANDBOX_MODULE_FILE).u8string() + "|" +
                            (dir / "no-such-config" / "scraps_sandbox_module.dll").u8string());
  }
  scraps_apps::GameTargets game_targets;
  game_targets.controls = &controls;
  game.install(game_targets);
  std::string game_message;
  double now = 0.0;
  char hud[64] = {};
  long hud_length = 0;  // used, so the HUD is not optimised away

  auto run = [&](int ticks) {
    for (int t = 0; t < ticks; ++t) {
      commands.clear();
      commands.push(make_command(CommandType::kMove, 0, Vec3(1.0f, 1.0f, 0.0f)));
      previous = world;
      sandbox::simulate(world, commands, world.tick + 1, &jobs);

      const std::size_t oldest = static_cast<std::size_t>(world.tick + 1) % server_kept.size();
      const std::size_t newest = static_cast<std::size_t>(world.tick) % server_kept.size();
      delta.clear();
      ByteWriter writer(delta);
      write_world_delta(writer, server_kept[oldest], world);
      ByteReader reader(delta.data(), delta.size());
      if (!read_world_delta(reader, client_kept[oldest], client)) ++bad_deltas;
      copy_state(world, server_kept[newest]);
      copy_state(client, client_kept[newest]);

      if (t % 30 == 0) mixer.play(beep_id, 0.5f);
      mixer.mix(mixed.data(), 800);
      input.move_pointer(400.0f + static_cast<float>(t % 50), 300.0f);
      if (t % 10 == 0) {
        input.press(MouseButton::kLeft);
      } else {
        input.release(MouseButton::kLeft);
      }
      input_backend.window().pump_events();
      for (std::unique_ptr<View>& view : views) {
        controls.sample(input, *view, camera, camera.target);
        picked.clear();
        controls.emit(*view, view->movement_rules(), 0, picked);
        const PointerPosition pointer = input.pointer();
        pick_sum += view->screen_to_world(Vec2(pointer.x, pointer.y), camera, camera.target).x;
      }
      now += 1.0 / 60.0;
      if (game.poll(now, game_targets, game_message) != scraps_apps::GameReload::kNone) ++bad_deltas;
      game.hud(world, 0, hud, sizeof(hud));
      hud_length += static_cast<long>(std::strlen(hud));
      SoundEvents events;
      game.sound_events(previous, world, 0, events);
      hud_length += static_cast<long>(events.count);
      for (std::unique_ptr<View>& view : views) {
        renderer.begin_frame(Color());
        view->draw(previous, world, 0.5f, camera, renderer, &sprites);
        renderer.end_frame();
      }
    }
  };

  run(60);  // warm-up: buffers grow to their working size
  const long before = g_allocations.load();
  run(300);
  const long allocations = g_allocations.load() - before;

  std::printf(
      "%ld allocations in 300 ticks of threaded simulate + world copy + delta encode/decode + mixing + mouse sampling "
      "and picking + hot game module polling, HUD and sounds + 3 textured view draws with shadows (%lu entities, "
      "pick sum %.1f, HUD %ld)\n",
      allocations, static_cast<unsigned long>(count_alive(world)), static_cast<double>(pick_sum), hud_length);
  if (bad_deltas != 0) {
    std::printf("FAILED: %ld deltas did not decode\n", bad_deltas);
    return 1;
  }

  // Pause (docs/specs/2026-11-14-offline-essentials-and-ui.md, part 4): an
  // offline game paused, ten seconds of paused frames in which both sides
  // poll and nothing ticks, then resumed, allocates nothing.
  long pause_allocations = 0;
  {
    Scene small;
    if (!load_scene(SCRAPS_SANDBOX_SCENE_DIR, "top-down", small, error)) {
      std::printf("FAILED: %s\n", error.c_str());
      return 1;
    }
    LoopbackHub hub;
    LoopbackNet server_net(hub, std::uint16_t{1});
    LoopbackNet client_net(hub, std::uint16_t{2});
    ServerConfig server_config;
    server_config.cookie_secret = CookieSecret{0x0123456789ABCDEFull, 0x1122334455667788ull};
    Server server(server_net, make_world(small, movement_rules_for(small.level.perspective)), &sandbox::simulate,
                  small.name, small.content_hash, server_config);
    ClientConfig client_config;
    client_config.scene_directory = SCRAPS_SANDBOX_SCENE_DIR;
    Client player(client_net, server_net.local_address(), &sandbox::simulate, client_config);
    FixedStepClock clock;
    scraps_apps::OfflinePause pause;
    double t = 0.0;
    const CommandList no_commands;
    auto frame = [&]() {  // the client app's loop, offline
      t += 1.0 / 60.0;
      hub.set_now(t);
      player.poll(t);
      const int steps = pause.advance(clock, 1.0 / 60.0);
      for (int s = 0; s < steps; ++s) {
        player.tick(no_commands, t);
        server.poll(t);
        server.tick();
        player.poll(t);
      }
    };
    for (int i = 0; i < 120; ++i) frame();
    if (find_entity(player.current_world(), player.local_entity()) == nullptr) {
      std::printf("FAILED: the offline player never appeared\n");
      return 1;
    }
    const long before_pause = g_allocations.load();
    pause.set(true, &server, player, clock);
    for (int i = 0; i < 600; ++i) {
      frame();
      server.poll(t);
    }
    pause.set(false, &server, player, clock);
    server.poll(t);
    player.poll(t);
    pause_allocations = g_allocations.load() - before_pause;
    std::printf("%ld allocations while pausing, 600 paused frames and resuming\n", pause_allocations);
    if (player.state() != ClientState::kConnected || server.client_count() != 1) {
      std::printf("FAILED: the paused player was disconnected\n");
      return 1;
    }
  }
  if (pause_allocations != 0) {
    std::printf("FAILED: pausing allocates\n");
    return 1;
  }
  if (allocations != 0) {
    std::printf("FAILED: the hot path allocates\n");
    return 1;
  }
  std::printf("OK\n");
  return 0;
}
