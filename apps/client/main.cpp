// scraps_client: the player's executable.
//
// Offline (default): runs the real Server inside this process over an
// in-memory LoopbackHub (Rule 5: offline = client + embedded server).
// Online (--connect host:port): talks to a scraps_server over UDP.
//
// The game's code comes through a GameApi (core/game_api.h): the sandbox
// linked into this exe, or with --hot-game (offline only) the sandbox module
// next to it, swapped in again whenever it is rebuilt (common/game_module.h).
//
// This file is the only place where the client picks concrete backends
// (Rule 3): SDL3 for a window, or headless for automated runs.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "backends/headless/headless_backend.h"
#include "backends/udp/udp_net.h"
#if SCRAPS_HAS_SDL3
#include "backends/sdl3/sdl3_backend.h"
#endif
#include "common/game_module.h"
#include "common/random_secret.h"
#include "common/scene_reload.h"
#include "core/fixed_step.h"
#include "core/jobs.h"
#include "core/mixer.h"
#include "core/scene.h"
#include "net/client.h"
#include "net/loopback.h"
#include "net/server.h"
#include "platform/services.h"
#include "sandbox/sandbox.h"
#include "views/controls.h"
#include "views/scene_music.h"
#include "views/sound_bank.h"
#include "views/sprite_bank.h"
#include "views/view_factory.h"

namespace {

using namespace scraps;

struct Options {
  std::string scene = "isometric";
  std::string scene_dir = SCRAPS_SANDBOX_SCENE_DIR;
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
      "usage: scraps_client [options]\n"
      "  --scene <name>        scene to play offline (default: isometric)\n"
      "                        bundled: top-down, side, isometric, crowd\n"
      "  --scene-dir <dir>     where .scene files are\n"
      "  --connect <ip:port>   join a scraps_server instead of playing offline\n"
      "  --view <name>         start in top-down, side or isometric view\n"
      "  --headless            no window: run --frames frames and print a summary\n"
      "  --frames <n>          quit after n frames (headless default: 600)\n"
      "  --no-autopilot        headless mode without scripted key presses\n"
      "  --threads <n>         worker threads for the simulation (default: cores - 1; 0 = none)\n"
      "  --hot-game            offline only: run the game from its module and reload it\n"
      "                        when rebuilt (cmake --build --preset mingw-game / msvc-game)\n"
      "keys: WASD/arrows move, Space jump, E drop coin, left click place coin, V next view,\n"
      "      -/= or wheel zoom, Esc quit\n"
      "offline, editing the .scene file reloads it while the game runs\n");
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
  return true;
}

// The networking side of a game session. Members are destroyed in reverse
// order: the job system and the hub outlive everything that uses them, and
// the game's code (maybe a loaded module) outlives everything that calls it.
struct Session {
  scraps_apps::GameModule game;
  std::unique_ptr<JobSystem> jobs;
  std::unique_ptr<LoopbackHub> hub;
  std::unique_ptr<Net> server_net;
  std::unique_ptr<Net> client_net;
  std::unique_ptr<Server> server;  // only when offline
  std::unique_ptr<Client> client;
  std::unique_ptr<scraps_apps::SceneReloader> reloader;  // only when offline
};

bool start_session(const Options& options, Session& session, std::string& error) {
  if (options.hot_game) {
    if (!session.game.load(SCRAPS_GAME_MODULE_FILE, error)) {
      error += std::string("\nbuild the game module with: ") + SCRAPS_GAME_BUILD_COMMAND;
      return false;
    }
    session.game.watch_other_builds(SCRAPS_GAME_MODULE_OTHER_BUILDS);
    std::printf("game '%s' loaded from %s\nwatching %s; rebuild it with: %s\n", session.game.name(),
                session.game.live_path().c_str(), SCRAPS_GAME_MODULE_FILE, SCRAPS_GAME_BUILD_COMMAND);
  } else if (!session.game.use_static(sandbox::game_api(), error)) {
    return false;
  }
  const SimulateFn simulate = session.game.api().simulate;

  const unsigned workers =
      options.threads < 0 ? JobSystem::default_worker_count() : static_cast<unsigned>(options.threads);
  session.jobs = std::make_unique<JobSystem>(workers);

  Address server_address;
  if (options.connect.empty()) {
    Scene scene;
    if (!load_scene(options.scene_dir, options.scene, scene, error)) return false;
    World world = make_world(scene, movement_rules_for(scene.level.perspective));
    session.hub = std::make_unique<LoopbackHub>();
    session.server_net = std::make_unique<LoopbackNet>(*session.hub, std::uint16_t{1});
    session.client_net = std::make_unique<LoopbackNet>(*session.hub, std::uint16_t{2});
    ServerConfig server_config;
    server_config.jobs = session.jobs.get();  // server and client run on this thread in turn
    server_config.cookie_secret = scraps_apps::random_cookie_secret();
    session.server = std::make_unique<Server>(*session.server_net, std::move(world), simulate, scene.name,
                                              scene.content_hash, server_config);
    session.reloader = std::make_unique<scraps_apps::SceneReloader>(options.scene_dir, options.scene);
    server_address = session.server_net->local_address();
  } else {
    if (!parse_address(options.connect, server_address)) {
      error = "cannot parse address '" + options.connect + "' (expected ip:port)";
      return false;
    }
    std::unique_ptr<udp::UdpNet> socket = udp::UdpNet::open(0, 0, error);
    if (!socket) return false;
    session.client_net = std::move(socket);
  }

  ClientConfig config;
  config.scene_directory = options.scene_dir;
  config.jobs = session.jobs.get();
  session.client = std::make_unique<Client>(*session.client_net, server_address, simulate, config);
  return true;
}

// Scripted input for headless runs: walk in a square, jump, drop a coin
// every 360 frames (in the side scene the player stands still then and picks
// it up again, so a long --hot-game run keeps counting coins), sweep the
// pointer and click once, and switch views, so a headless run exercises the
// whole engine.
void drive_autopilot(headless::HeadlessInput& input, long frame, float width, float height) {
  const Key walk[4] = {Key::kD, Key::kS, Key::kA, Key::kW};
  const long phase = (frame / 90) % 4;
  for (long i = 0; i < 4; ++i) {
    if (i == phase) {
      input.press(walk[i]);
    } else {
      input.release(walk[i]);
    }
  }
  auto tap = [&input](Key key, bool now) {
    if (now) {
      input.press(key);
    } else {
      input.release(key);
    }
  };
  tap(Key::kSpace, frame % 45 == 20);
  tap(Key::kE, frame % 360 == 200);
  tap(Key::kV, frame == 300 || frame == 400);
  // A slow circle a little right of and below the screen centre (the player),
  // about a cell away at the default zoom.
  const float angle = static_cast<float>(frame) * 0.05f;
  input.move_pointer(width * 0.5f + 40.0f + 30.0f * std::cos(angle), height * 0.5f + 30.0f + 20.0f * std::sin(angle));
  if (frame == 250) {
    input.press(MouseButton::kLeft);
  } else {
    input.release(MouseButton::kLeft);
  }
  if (frame == 350) input.scroll(1.0f);
}

// Outlines the grid cell that holds `point`: its twelve edges, projected.
// Seen straight on (top-down, side) they fold into a square; in the
// isometric view they make a wire cube. Presentation only.
void draw_pointer_cell(const View& view, const Camera& camera, const TileGrid& grid, const Vec3& point,
                       Renderer2D& renderer) {
  // The same cell a click there picks (feet on a block top: the cell above it).
  CellCoord cell;
  if (!cell_at_point(point, cell)) return;
  const int cx = cell.x;
  const int cy = cell.y;
  const int cz = cell.z;
  if (cx < 0 || cy < 0 || cx >= grid.width || cy >= grid.depth) return;
  Vec2 corner[8];
  for (int i = 0; i < 8; ++i) {
    corner[i] = view.world_to_screen(Vec3(static_cast<float>(cx + (i & 1)), static_cast<float>(cy + ((i >> 1) & 1)),
                                          static_cast<float>(cz + ((i >> 2) & 1))),
                                     camera);
  }
  const Color color(255, 255, 255, 170);
  for (int i = 0; i < 8; ++i) {
    for (int bit = 1; bit < 8; bit <<= 1) {
      if ((i & bit) != 0) continue;  // each edge once, from its lower corner
      renderer.draw_line(corner[i].x, corner[i].y, corner[i | bit].x, corner[i | bit].y, color);
    }
  }
}

// The camera follows the local player in the world being drawn.
Vec3 focus_point(const World& shown, EntityId me_id) {
  const Entity* me = find_entity(shown, me_id);
  if (me == nullptr) {
    const TileGrid& grid = shown.level.grid;
    return Vec3(static_cast<float>(grid.width) * 0.5f, static_cast<float>(grid.depth) * 0.5f, 1.0f);
  }
  return me->position + Vec3(0.0f, 0.0f, me->size.z * 0.5f);
}

int run(const Options& options, PlatformServices platform, headless::HeadlessInput* autopilot, bool fixed_frame_time) {
  Session session;
  std::string error;
  if (!start_session(options, session, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  Client& client = *session.client;

  // The game's own bindings (in the sandbox: E drops a coin, a left click
  // places one); a reloaded module brings its own.
  Controls controls;
  scraps_apps::GameTargets game_targets;
  game_targets.server = session.server.get();
  game_targets.client = &client;
  game_targets.controls = &controls;
  session.game.install(game_targets);
  char game_hud[64] = {};
  char last_game_hud[64] = {};

  std::unique_ptr<View> view;
  Camera camera;
  Vec3 pointer_through;  // the pointer plane goes through the local player's feet, as last drawn
  SpriteBank sprites;  // declared after `platform` is created, destroyed before it
  std::uint32_t sprites_generation = 0;
  // Sound effects: decoded into the mixer, mixed here each frame and handed
  // to the backend a little ahead (about 60 ms queued).
  Mixer mixer;
  SoundBank sounds;
  SceneMusic music;  // the scene's music line, streamed; kept across a hot reload of the same file
  constexpr std::size_t kAudioAheadFrames = kSoundSampleRate * 60 / 1000;
  std::vector<float> mix_buffer(kAudioAheadFrames * kSoundChannels);
  std::size_t sounds_played = 0;
  std::string notice;  // a short message on the HUD (reloads, art errors)
  double notice_until = 0.0;
  if (session.game.hot()) {
    // Where hot reload looks, and how to feed it.
    notice = std::string("hot game: rebuild with ") + SCRAPS_GAME_BUILD_COMMAND;
    notice_until = 8.0;
  }
  FixedStepClock clock;
  const auto start = std::chrono::steady_clock::now();
  double now = 0.0;
  double last = 0.0;
  double fps = 0.0;
  double fps_window_start = 0.0;
  long fps_frames = 0;
  long frame = 0;
  long ticks = 0;

  while (!platform.window->close_requested() && (options.frames == 0 || frame < options.frames)) {
    // Rule 2 and the "only the loop reads the clock" code rule: time is
    // measured here, once per frame, and passed down explicitly.
    if (fixed_frame_time) {
      now += 1.0 / 60.0;
    } else {
      now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    const double frame_seconds = now - last;
    last = now;

    if (autopilot != nullptr) {
      drive_autopilot(*autopilot, frame, static_cast<float>(platform.renderer->output_width()),
                      static_cast<float>(platform.renderer->output_height()));
    }
    platform.window->pump_events();
    const Input& input = *platform.input;
    if (input.was_pressed(Key::kEscape)) break;

    if (session.hub) session.hub->set_now(now);
    client.poll(now);
    if (client.state() != ClientState::kConnecting && client.state() != ClientState::kConnected) {
      std::fprintf(stderr, "client %s: %s\n", client_state_name(client.state()), client.error().c_str());
      return 1;
    }

    // Offline hot reload: an edited scene file replaces the running world.
    std::string message;
    if (session.reloader && session.server && session.reloader->poll(now, *session.server, message)) {
      std::printf("%s\n", message.c_str());
      notice = message;
      notice_until = now + 4.0;
    }
    // Hot game code: a rebuilt module replaces the running game code here,
    // between frames (no tick or job runs now). The World stays. The file is
    // checked by the wall clock even when a headless run simulates its time:
    // the linker writing it works in real time.
    const double wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (session.game.hot() &&
        session.game.poll(wall_seconds, game_targets, message) != scraps_apps::GameReload::kNone) {
      std::printf("%s\n", message.c_str());
      std::fflush(stdout);
      notice = message;
      notice_until = now + 4.0;
    }

    // A new world (first connection or a reload) brings its own images and sounds.
    if (client.has_world() && client.world_generation() != sprites_generation) {
      sprites_generation = client.world_generation();
      std::string errors;
      if (!sprites.load(client.current_world().level, options.scene_dir, *platform.renderer, errors)) {
        std::fprintf(stderr, "some images failed to load (drawn as flat colours):\n%s", errors.c_str());
        notice = "some images failed to load; see the console";
        notice_until = now + 6.0;
      }
      if (!sounds.load(client.current_world().level, options.scene_dir, mixer, errors)) {
        std::fprintf(stderr, "some sounds failed to load (they stay silent):\n%s", errors.c_str());
        notice = "some sounds failed to load; see the console";
        notice_until = now + 6.0;
      }
      if (!music.update(client.current_world().level, options.scene_dir, mixer, errors)) {
        std::fprintf(stderr, "the music failed to load (it stays silent): %s\n", errors.c_str());
        notice = "the music failed to load; see the console";
        notice_until = now + 6.0;
      }
    }

    if (client.has_world() && !view) {
      Perspective perspective = client.current_world().level.perspective;
      if (!options.view.empty()) parse_perspective(options.view, perspective);
      view = make_view(perspective);
      camera.zoom = view->default_zoom();
    }
    // Clicks are picked first, with the view, camera and pointer plane of the
    // last drawn frame: what the player saw when they clicked. The view swap
    // and the zoom below only change the next drawn frame.
    if (view) {
      controls.sample(input, *view, camera, pointer_through);
    } else {
      controls.sample(input);
    }
    if (view && input.was_pressed(Key::kV)) {
      view = make_view(next_perspective(view->perspective()));
      camera.zoom = view->default_zoom();
      const std::string title = std::string("Scraps - ") + view->name();
      platform.window->set_title(title.c_str());
    }
    if (input.was_pressed(Key::kMinus)) camera.zoom = std::max(6.0f, camera.zoom / 1.25f);
    if (input.was_pressed(Key::kEquals)) camera.zoom = std::min(200.0f, camera.zoom * 1.25f);
    const float wheel = input.wheel();
    if (wheel != 0.0f && std::isfinite(wheel)) {
      const float notches = std::max(-4.0f, std::min(4.0f, wheel));  // a fast spin is still a few steps
      camera.zoom = std::max(6.0f, std::min(200.0f, camera.zoom * std::pow(1.25f, notches)));
    }

    const int steps = clock.advance(frame_seconds);
    for (int step = 0; step < steps; ++step) {
      CommandList commands;
      if (view && client.state() == ClientState::kConnected) {
        const World& world = client.current_world();
        controls.emit(*view, world.rules, client.player_id(), commands);
      }
      client.tick(commands, now);
      if (client.state() == ClientState::kConnected) {
        // The game decides what this tick sounds like (presentation only).
        SoundEvents events;
        session.game.sound_events(client.previous_world(), client.current_world(), client.player_id(), events);
        for (std::size_t e = 0; e < events.count; ++e) sounds.play(events.name(e), mixer);
        sounds_played += events.count;
      }
      if (session.server) {
        // Offline: the embedded server runs its tick right here, in step
        // with the client, through the same code a dedicated server runs.
        session.server->poll(now);
        session.server->tick();
        client.poll(now);
      }
      ++ticks;
    }

    Renderer2D& renderer = *platform.renderer;
    const World& world = client.current_world();
    renderer.begin_frame(client.has_world() ? world.level.sky_bottom : kDefaultSky);
    if (client.has_world()) draw_sky(world.level, renderer);
    if (view && client.has_world()) {
      camera.viewport_width = static_cast<float>(renderer.output_width());
      camera.viewport_height = static_cast<float>(renderer.output_height());
      // display_world: predicted entities blended by alpha, other players
      // interpolated between snapshots. Already blended, so drawn with alpha 0.
      const World& shown = client.display_world(clock.alpha());
      camera.target = focus_point(shown, client.local_entity());
      view->draw(shown, shown, 0.0f, camera, renderer, &sprites);

      // A small marker above the local player.
      const Entity* me = find_entity(shown, client.local_entity());
      pointer_through = me != nullptr ? me->position : camera.target;
      if (input.pointer_inside()) {
        const PointerPosition pointer = input.pointer();
        const Vec3 hovered = view->screen_to_world(Vec2(pointer.x, pointer.y), camera, pointer_through);
        draw_pointer_cell(*view, camera, shown.level.grid, hovered, renderer);
      }
      if (me != nullptr) {
        // Placed in screen space, above where the sprite's head is drawn: a
        // world-space "above" would land on the body in the top-down view,
        // which has no height on screen.
        const Vec2 feet = view->world_to_screen(me->position, camera);
        const Vec2 tip(feet.x, feet.y - me->size.z * camera.zoom * 1.6f - 8.0f);
        const Vertex2D marker[3] = {Vertex2D(tip.x - 6.0f, tip.y - 10.0f, Color(255, 255, 255)),
                                    Vertex2D(tip.x + 6.0f, tip.y - 10.0f, Color(255, 255, 255)),
                                    Vertex2D(tip.x, tip.y, Color(255, 255, 255))};
        renderer.fill_triangles(marker, 3, kNoTexture);
      }
    }

    session.game.hud(world, client.player_id(), game_hud, sizeof(game_hud));
    if (options.headless && options.hot_game && std::strcmp(game_hud, last_game_hud) != 0) {
      // A headless hot-reload session has no screen: print the game's HUD
      // text whenever it changes, so the effect of a reload can be seen.
      std::printf("tick %u: %s\n", static_cast<unsigned>(world.tick), game_hud);
      std::fflush(stdout);
      std::memcpy(last_game_hud, game_hud, sizeof(game_hud));
    }
    int players = 0;
    for (const Entity& entity : world.entities) {
      if (entity.alive && entity.player != kNoPlayer) ++players;
    }
    // A dark strip behind the HUD keeps the text readable over bright art.
    RectF hud_strip;
    hud_strip.w = static_cast<float>(renderer.output_width());
    hud_strip.h = now < notice_until ? 66.0f : 52.0f;
    renderer.fill_rect(hud_strip, Color(0, 0, 0, 150));
    char line[256];
    std::snprintf(line, sizeof(line), "Scraps  %s  scene %s  view %s  tick %u  %.0f fps",
                  session.server ? "offline" : options.connect.c_str(),
                  client.scene_name().empty() ? "-" : client.scene_name().c_str(), view ? view->name() : "-",
                  static_cast<unsigned>(world.tick), fps);
    renderer.draw_debug_text(10.0f, 10.0f, line, Color(240, 240, 240));
    std::snprintf(line, sizeof(line), "%s  players %d  %s  rtt %.0f ms  unacked inputs %u",
                  client_state_name(client.state()), players, game_hud, client.round_trip_seconds() * 1000.0,
                  static_cast<unsigned>(client.unacknowledged_inputs()));
    renderer.draw_debug_text(10.0f, 24.0f, line, Color(200, 200, 210));
    renderer.draw_debug_text(10.0f, 38.0f,
                             "WASD move  Space jump  E drop coin  Click place coin  V view  -/= wheel zoom  Esc quit",
                             Color(150, 150, 165));
    if (now < notice_until) renderer.draw_debug_text(10.0f, 52.0f, notice.c_str(), Color(255, 220, 120));
    renderer.end_frame();

    // Keep the audio device about 60 ms ahead.
    const std::size_t queued = platform.audio->queued_frames();
    if (queued < kAudioAheadFrames) {
      const std::size_t frames = kAudioAheadFrames - queued;
      mixer.mix(mix_buffer.data(), frames);
      platform.audio->submit(mix_buffer.data(), frames);
    }

    // A headless client talking to a real server has no vsync to pace it;
    // sleep for about one 60 Hz frame so its clock matches the server's.
    if (options.headless && !fixed_frame_time) std::this_thread::sleep_for(std::chrono::milliseconds(16));

    ++frame;
    ++fps_frames;
    if (now - fps_window_start >= 0.5) {
      fps = static_cast<double>(fps_frames) / (now - fps_window_start);
      fps_frames = 0;
      fps_window_start = now;
    }
  }

  client.disconnect();
  if (session.server) session.server->poll(now);  // let the embedded server see the Bye

  if (options.headless) {
    const World& world = client.current_world();
    const Entity* me = find_entity(world, client.local_entity());
    std::printf("headless run: %ld frames, %ld ticks, world tick %u, client %s\n", frame, ticks,
                static_cast<unsigned>(world.tick), client_state_name(client.state()));
    if (me == nullptr) {
      std::printf("FAILED: the local player never appeared\n");
      return 1;
    }
    char hud[64] = {};
    session.game.hud(world, client.player_id(), hud, sizeof(hud));
    std::printf(
        "player at (%.2f, %.2f, %.2f), %s, entities %lu, images %lu, sounds %lu (%lu played), threads %u\n",
        me->position.x, me->position.y, me->position.z, hud,
        static_cast<unsigned long>(count_alive(world)), static_cast<unsigned long>(sprites.loaded_count()),
        static_cast<unsigned long>(mixer.sound_count()), static_cast<unsigned long>(sounds_played),
        session.jobs->worker_count());
    const MusicStream* playing = mixer.current_music();
    if (playing == nullptr) {
      std::printf("music: none\n");
    } else {
      std::printf("music: %s (%s, wrapped to its start %u times)\n", music.path().c_str(),
                  audio_format_name(playing->format()), static_cast<unsigned>(playing->loops()));
    }
  }
  return 0;
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
