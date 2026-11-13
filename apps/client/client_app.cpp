// scraps_client's frame (client_app.h). Moved here from main() unchanged in
// what it does without a UI; with one, the UI model (common/ui_model.h)
// decides the screen and the app carries out what the UI asks for.
//
// This file names concrete backends (Rule 3), as apps may: udp for online
// play, sdl3 for the player's save folder, and the headless input for the
// autopilot.

#include "client/client_app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "backends/headless/headless_backend.h"
#include "backends/udp/udp_net.h"
#if SCRAPS_HAS_SDL3
#include "backends/sdl3/sdl3_backend.h"
#endif
#include "common/game_module.h"
#include "common/pause.h"
#include "common/random_secret.h"
#include "common/save_game.h"
#include "common/scene_reload.h"
#include "common/ui_model.h"
#include "core/fixed_step.h"
#include "core/jobs.h"
#include "core/mixer.h"
#include "core/scene.h"
#include "net/client.h"
#include "net/loopback.h"
#include "net/server.h"
#include "selected_game.h"
#include "views/controls.h"
#include "views/scene_music.h"
#include "views/sound_bank.h"
#include "views/sprite_bank.h"
#include "views/view_factory.h"

namespace scraps_apps {
namespace {

using namespace scraps;

// The networking side of a game session. Members are destroyed in reverse
// order: the job system and the hub outlive everything that uses them, and
// the game's code (maybe a loaded module) outlives everything that calls it.
struct Session {
  GameModule game;
  std::unique_ptr<JobSystem> jobs;
  std::unique_ptr<LoopbackHub> hub;
  std::unique_ptr<Net> server_net;
  std::unique_ptr<Net> client_net;
  std::unique_ptr<Server> server;  // only when offline
  std::unique_ptr<Client> client;
  std::unique_ptr<SceneReloader> reloader;  // only when offline
};

bool start_session(const ClientOptions& options, Session& session, std::string& error) {
  const SelectedGame& selected = selected_game();
  if (options.hot_game) {
    if (!session.game.load(selected.module_file, error)) {
      error += std::string("\nbuild the game module with: ") + selected.build_command;
      return false;
    }
    session.game.watch_other_builds(selected.other_builds);
    std::printf("game '%s' loaded from %s\nwatching %s; rebuild it with: %s\n", session.game.name(),
                session.game.live_path().c_str(), selected.module_file, selected.build_command);
  } else if (!session.game.use_static(selected_game_api(), error)) {
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
    server_config.cookie_secret = random_cookie_secret();
    session.server = std::make_unique<Server>(*session.server_net, std::move(world), simulate, scene.name,
                                              scene.content_hash, server_config);
    session.reloader = std::make_unique<SceneReloader>(options.scene_dir, options.scene);
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
// pointer and click once, switch views, and plug in a gamepad for a while
// (stick, south, west), so a headless run exercises the whole engine.
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
  // Offline: quick save, a one-second pause, then load the save again.
  tap(Key::kF5, frame == 240);
  tap(Key::kP, frame == 270 || frame == 330);
  tap(Key::kF9, frame == 420);
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

  // Frames 450 to 569: a gamepad. The keys above still walk; the stick is
  // only read while no movement key is held, so it shows in the HUD notice
  // and in the commands, not in where the walk ends.
  if (frame == 450) input.connect_gamepad(true);
  if (frame == 570) input.connect_gamepad(false);
  if (frame >= 450 && frame < 570) {
    input.set_axis(GamepadAxis::kLeftX, 0.7f * std::cos(angle));
    input.set_axis(GamepadAxis::kLeftY, 0.7f * std::sin(angle));
    auto pad_tap = [&input](GamepadButton button, bool now) {
      if (now) {
        input.press(button);
      } else {
        input.release(button);
      }
    };
    pad_tap(GamepadButton::kSouth, frame == 470);
    pad_tap(GamepadButton::kWest, frame == 500);
  }
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

// Where quick saves go: the player's own folder from SDL (on Windows
// %APPDATA%\Scraps\<game>\), or ./saves/ without a window. Decided here, in
// the app, never in core (Rule 3).
std::string save_directory(const ClientOptions& options, const char* game_name) {
  if (!options.save_dir.empty()) return options.save_dir;
#if SCRAPS_HAS_SDL3
  if (!options.headless) {
    const std::string path = sdl3::preferences_path("Scraps", game_name);
    if (!path.empty()) return path;
  }
#else
  (void)game_name;
#endif
  return "saves/";
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

// The longest notice handed to the UI; a longer one is cut (the UTF-8 writer
// turns a character cut in half into '?').
constexpr std::size_t kUiNoticeBytes = 255;

}  // namespace

// Everything run() used to keep in local variables, in the same order, so
// it is destroyed in the same order.
struct ClientApp::State {
  State(const ClientOptions& opts, PlatformServices services, headless::HeadlessInput* pilot)
      : options(opts), platform(services), autopilot(pilot) {}

  ClientOptions options;
  PlatformServices platform;
  headless::HeadlessInput* autopilot;

  Session session;
  Controls controls;
  GameTargets game_targets;
  char game_hud[64] = {};
  char last_game_hud[64] = {};

  std::unique_ptr<View> view;
  Camera camera;
  Vec3 pointer_through;  // the pointer plane goes through the local player's feet, as last drawn
  SpriteBank sprites;    // created after `platform`, destroyed before it
  std::uint32_t sprites_generation = 0;
  // Sound effects: decoded into the mixer, mixed here each frame and handed
  // to the backend a little ahead (about 60 ms queued).
  Mixer mixer;
  SoundBank sounds;
  SceneMusic music;  // the scene's music line, streamed; kept across a hot reload of the same file
  static constexpr std::size_t kAudioAheadFrames = kSoundSampleRate * 60 / 1000;
  std::vector<float> mix_buffer = std::vector<float>(kAudioAheadFrames * kSoundChannels);
  std::size_t sounds_played = 0;
  std::string notice;  // a short message on the HUD (reloads, art errors)
  double notice_until = 0.0;
  FixedStepClock clock;
  // Offline only: pause (P) and the quick save slot (F5 saves, F9 loads).
  OfflinePause pause;
  std::string quick_save_path;
  bool has_save = false;
  ClientAppStats stats;
  double now = 0.0;
  double last = 0.0;
  double fps = 0.0;
  double fps_window_start = 0.0;
  long fps_frames = 0;
  bool gamepad_was_connected = false;
  bool failed = false;  // the connection failed: finish() returns 1 at once

  // The UI: what it shows, and the input the game controls see without it.
  bool ui_present = false;
  std::unique_ptr<UiModel> ui;
  UiRequests requests;
  NoInput no_input;
  bool game_had_input = true;
  char ui_notice[kUiNoticeBytes + 1] = {};

  // P, a gamepad's start button, or the UI: the one place that pauses and
  // resumes (common/pause.h), with its notice.
  void set_paused(bool paused) {
    if (!pause.set(paused, session.server.get(), *session.client, clock)) {
      notice = "pause works offline only";
    } else {
      notice = pause.paused() ? "paused" : "resumed";
    }
    notice_until = now + 2.0;
  }

  // F5 (save) or F9 (load), and the UI's save, load and continue: one path.
  // Saving reads, and loading replaces, the embedded server's World between
  // ticks (Rule 5: the server holds the true World). True if it worked.
  bool save_or_load(bool save) {
    bool worked = false;
    std::string save_error;
    if (!session.server) {
      notice = "save and load work offline only";
    } else if (save) {
      if (save_game(*session.server, session.game.name(), quick_save_path, save_error)) {
        ++stats.saves;
        has_save = true;
        worked = true;
        notice = "saved to " + quick_save_path;
      } else {
        ++stats.save_errors;
        notice = "save failed: " + save_error;
      }
    } else {
      SaveHeader header;
      const SaveLoad result =
          load_game(*session.server, session.game.name(), options.scene_dir, quick_save_path, header, save_error);
      if (result == SaveLoad::kRefused) {
        ++stats.save_errors;
        notice = "load failed: " + save_error;
      } else {
        ++stats.loads;
        worked = true;
        pause.set(false, session.server.get(), *session.client, clock);  // a loaded game starts running
        notice = "loaded " + quick_save_path + " (tick " + std::to_string(header.tick) + ")";
        if (result == SaveLoad::kSceneChanged) {
          notice += "; warning: the scene file changed since this save";
        }
      }
    }
    std::printf("%s\n", notice.c_str());
    std::fflush(stdout);
    notice_until = now + 4.0;
    return worked;
  }

  // Carries out what the UI asked for. False: quit.
  bool carry_out(const UiRequests& asked) {
    for (std::size_t i = 0; i < asked.count; ++i) {
      switch (asked.items[i]) {
        case UiRequest::kQuit:
          return false;
        case UiRequest::kPlay:
          ui->mark_started();
          clock.reset();  // the menu's time is never caught up
          break;
        case UiRequest::kContinue:
          if (save_or_load(false)) {
            ui->mark_started();
            clock.reset();
          }
          break;
        case UiRequest::kPause:
          if (!pause.paused()) set_paused(true);
          break;
        case UiRequest::kResume:
          if (pause.paused()) set_paused(false);
          break;
        case UiRequest::kSave:
          save_or_load(true);
          break;
        case UiRequest::kLoad:
          save_or_load(false);
          break;
      }
    }
    return true;
  }

  bool game_has_input() const { return ui->game_has_input(); }
};

ClientApp::ClientApp(const ClientOptions& options, PlatformServices platform, headless::HeadlessInput* autopilot)
    : m(std::make_unique<State>(options, platform, autopilot)) {}

ClientApp::~ClientApp() = default;

bool ClientApp::start(std::string& error) {
  State& s = *m;
  if (s.options.scene.empty()) s.options.scene = selected_game().default_scene;
  if (!start_session(s.options, s.session, error)) return false;
  Client& client = *s.session.client;

  // The game's own bindings (in the sandbox: E drops a coin, a left click
  // places one); a reloaded module brings its own.
  s.game_targets.server = s.session.server.get();
  s.game_targets.client = &client;
  s.game_targets.controls = &s.controls;
  s.session.game.install(s.game_targets);
  if (s.session.game.hot()) {
    // Where hot reload looks, and how to feed it.
    s.notice = std::string("hot game: rebuild with ") + selected_game().build_command;
    s.notice_until = 8.0;
  }
  if (s.session.server) {
    s.quick_save_path = save_directory(s.options, s.session.game.name()) + "quick.sav";
    std::error_code exists_error;
    std::filesystem::path quick_save;
    std::string path_error;
    s.has_save = save_path(s.quick_save_path, quick_save, path_error) &&
                 std::filesystem::exists(quick_save, exists_error);
  }
  s.ui_present = s.platform.ui != nullptr && s.platform.ui->present();
  s.ui = std::make_unique<UiModel>(s.pause, !s.session.server, s.ui_present);
  return true;
}

bool ClientApp::frame(double now, double wall_seconds) {
  State& s = *m;
  const ClientOptions& options = s.options;
  PlatformServices& platform = s.platform;
  if (platform.window->close_requested() || (options.frames != 0 && s.stats.frames >= options.frames)) return false;
  Session& session = s.session;
  Client& client = *session.client;

  // Rule 2 and the "only the loop reads the clock" code rule: time is
  // measured by the caller, once per frame, and passed down explicitly.
  s.now = now;
  const double frame_seconds = now - s.last;
  s.last = now;

  if (s.autopilot != nullptr) {
    drive_autopilot(*s.autopilot, s.stats.frames, static_cast<float>(platform.renderer->output_width()),
                    static_cast<float>(platform.renderer->output_height()));
  }
  platform.window->pump_events();
  const Input& input = *platform.input;
  // What the UI said since the last frame, carried out before the keys.
  s.requests.count = 0;
  if (s.ui_present) {
    s.ui->receive(*platform.ui, session.game, s.requests);
    if (!s.carry_out(s.requests)) return false;
  }
  if (input.was_pressed(Key::kEscape)) {
    if (!s.ui_present) return false;  // without a UI, Esc quits
    s.requests.count = 0;
    s.ui->escape(s.requests);  // with one, it opens and closes the menus
    if (!s.carry_out(s.requests)) return false;
  }
  if (input.gamepad_connected() != s.gamepad_was_connected) {
    // Controls read the pad through Input like the keys; this only says so.
    s.gamepad_was_connected = input.gamepad_connected();
    s.notice = s.gamepad_was_connected ? "gamepad connected" : "gamepad disconnected";
    s.notice_until = now + 3.0;
    std::printf("%s\n", s.notice.c_str());
  }

  // On the main menu nothing runs yet: no network, no ticks. The client
  // starts connecting at its first poll, once the player chose to play.
  const bool running = s.ui->started();
  if (running) {
    if (session.hub) session.hub->set_now(now);
    client.poll(now);
    if (client.state() != ClientState::kConnecting && client.state() != ClientState::kConnected) {
      std::fprintf(stderr, "client %s: %s\n", client_state_name(client.state()), client.error().c_str());
      s.failed = true;
      return false;
    }
  }

  // Offline hot reload: an edited scene file replaces the running world.
  std::string message;
  if (session.reloader && session.server && session.reloader->poll(now, *session.server, message)) {
    std::printf("%s\n", message.c_str());
    s.notice = message;
    s.notice_until = now + 4.0;
  }
  // Hot game code: a rebuilt module replaces the running game code here,
  // between frames (no tick or job runs now). The World stays. The file is
  // checked by the wall clock even when a headless run simulates its time:
  // the linker writing it works in real time.
  if (session.game.hot() && session.game.poll(wall_seconds, s.game_targets, message) != GameReload::kNone) {
    std::printf("%s\n", message.c_str());
    std::fflush(stdout);
    s.notice = message;
    s.notice_until = now + 4.0;
  }

  // A new world (first connection or a reload) brings its own images and sounds.
  if (client.has_world() && client.world_generation() != s.sprites_generation) {
    s.sprites_generation = client.world_generation();
    std::string errors;
    if (!s.sprites.load(client.current_world().level, options.scene_dir, *platform.renderer, errors)) {
      std::fprintf(stderr, "some images failed to load (drawn as flat colours):\n%s", errors.c_str());
      s.notice = "some images failed to load; see the console";
      s.notice_until = now + 6.0;
    }
    if (!s.sounds.load(client.current_world().level, options.scene_dir, s.mixer, errors)) {
      std::fprintf(stderr, "some sounds failed to load (they stay silent):\n%s", errors.c_str());
      s.notice = "some sounds failed to load; see the console";
      s.notice_until = now + 6.0;
    }
    if (!s.music.update(client.current_world().level, options.scene_dir, s.mixer, errors)) {
      std::fprintf(stderr, "the music failed to load (it stays silent): %s\n", errors.c_str());
      s.notice = "the music failed to load; see the console";
      s.notice_until = now + 6.0;
    }
  }

  if (client.has_world() && !s.view) {
    Perspective perspective = client.current_world().level.perspective;
    if (!options.view.empty()) parse_perspective(options.view, perspective);
    s.view = make_view(perspective);
    s.camera.zoom = s.view->default_zoom();
  }
  // The game controls see the input only while the game is played: not
  // while paused, and not in a menu (a click on a menu button is not a click
  // in the world). Otherwise they sample nothing, so nothing is latched to
  // act after a resume.
  const bool game_input = s.game_has_input();
  const Input& controls_input = game_input ? input : static_cast<const Input&>(s.no_input);
  // The app's own keys (V, zoom, F5, F9) and the pointer cell: always
  // without a UI (F9 also resumes a paused game); with one, only while
  // playing, since the menus own the keyboard and mouse otherwise.
  const bool app_keys = !s.ui_present || game_input;
  // Clicks are picked first, with the view, camera and pointer plane of the
  // last drawn frame: what the player saw when they clicked. The view swap
  // and the zoom below only change the next drawn frame.
  if (s.view) {
    s.controls.sample(controls_input, *s.view, s.camera, s.pointer_through);
  } else {
    s.controls.sample(controls_input);
  }
  if (app_keys && s.view && input.was_pressed(Key::kV)) {
    s.view = make_view(next_perspective(s.view->perspective()));
    s.camera.zoom = s.view->default_zoom();
    const std::string title = std::string("Scraps - ") + s.view->name();
    platform.window->set_title(title.c_str());
  }
  // Pause, offline only (P or a gamepad's start button). With a UI the same
  // keys open and close the pause menu, which pauses through the same path.
  if (input.was_pressed(Key::kP) || input.was_pressed(GamepadButton::kStart)) {
    if (!s.ui_present) {
      s.set_paused(!s.pause.paused());
    } else {
      s.requests.count = 0;
      s.ui->toggle_pause(s.requests);
      s.carry_out(s.requests);  // never asks to quit
    }
  }
  if (app_keys && (input.was_pressed(Key::kF5) || input.was_pressed(Key::kF9))) {
    s.save_or_load(input.was_pressed(Key::kF5));
  }
  if (app_keys) {
    if (input.was_pressed(Key::kMinus)) s.camera.zoom = std::max(6.0f, s.camera.zoom / 1.25f);
    if (input.was_pressed(Key::kEquals)) s.camera.zoom = std::min(200.0f, s.camera.zoom * 1.25f);
    const float wheel = input.wheel();
    if (wheel != 0.0f && std::isfinite(wheel)) {
      const float notches = std::max(-4.0f, std::min(4.0f, wheel));  // a fast spin is still a few steps
      s.camera.zoom = std::max(6.0f, std::min(200.0f, s.camera.zoom * std::pow(1.25f, notches)));
    }
  }
  // Whenever the game gains or loses the input (resume, play, continue, a
  // load, a menu opening), presses latched before do not act afterwards.
  if (s.game_has_input() != s.game_had_input) {
    s.game_had_input = s.game_has_input();
    s.controls.clear_latches();
  }

  const int steps = running ? s.pause.advance(s.clock, frame_seconds) : 0;
  if (s.pause.paused()) {
    ++s.stats.paused_frames;
    s.stats.ticks_while_paused += steps;
  }
  for (int step = 0; step < steps; ++step) {
    CommandList commands;
    if (s.view && client.state() == ClientState::kConnected) {
      const World& world = client.current_world();
      s.controls.emit(*s.view, world.rules, client.player_id(), commands);
      // The UI's game commands (a "drop coin" button) join the next tick's,
      // for the local player, like a bound key's (Rule 1).
      const std::size_t before = commands.size();
      s.ui->take_commands(client.player_id(), commands);
      s.stats.ui_commands += static_cast<long>(commands.size() - before);
    }
    for (const Command& command : commands) {
      if (command.type != CommandType::kMove) ++s.stats.game_commands;
    }
    client.tick(commands, now);
    if (client.state() == ClientState::kConnected) {
      // The game decides what this tick sounds like (presentation only).
      SoundEvents events;
      session.game.sound_events(client.previous_world(), client.current_world(), client.player_id(), events);
      for (std::size_t e = 0; e < events.count; ++e) s.sounds.play(events.name(e), s.mixer);
      s.sounds_played += events.count;
    }
    if (session.server) {
      // Offline: the embedded server runs its tick right here, in step
      // with the client, through the same code a dedicated server runs.
      session.server->poll(now);
      session.server->tick();
      client.poll(now);
    }
    ++s.stats.ticks;
  }

  Renderer2D& renderer = *platform.renderer;
  const World& world = client.current_world();
  renderer.begin_frame(client.has_world() ? world.level.sky_bottom : kDefaultSky);
  if (client.has_world()) draw_sky(world.level, renderer);
  if (s.view && client.has_world()) {
    s.camera.viewport_width = static_cast<float>(renderer.output_width());
    s.camera.viewport_height = static_cast<float>(renderer.output_height());
    // display_world: predicted entities blended by alpha, other players
    // interpolated between snapshots. Already blended, so drawn with alpha 0.
    const World& shown = client.display_world(s.clock.alpha());
    s.camera.target = focus_point(shown, client.local_entity());
    s.view->draw(shown, shown, 0.0f, s.camera, renderer, &s.sprites);

    // A small marker above the local player.
    const Entity* me = find_entity(shown, client.local_entity());
    s.pointer_through = me != nullptr ? me->position : s.camera.target;
    if (app_keys && input.pointer_inside()) {
      const PointerPosition pointer = input.pointer();
      const Vec3 hovered = s.view->screen_to_world(Vec2(pointer.x, pointer.y), s.camera, s.pointer_through);
      draw_pointer_cell(*s.view, s.camera, shown.level.grid, hovered, renderer);
    }
    if (me != nullptr) {
      // Placed in screen space, above where the sprite's head is drawn: a
      // world-space "above" would land on the body in the top-down view,
      // which has no height on screen.
      const Vec2 feet = s.view->world_to_screen(me->position, s.camera);
      const Vec2 tip(feet.x, feet.y - me->size.z * s.camera.zoom * 1.6f - 8.0f);
      const Vertex2D marker[3] = {Vertex2D(tip.x - 6.0f, tip.y - 10.0f, Color(255, 255, 255)),
                                  Vertex2D(tip.x + 6.0f, tip.y - 10.0f, Color(255, 255, 255)),
                                  Vertex2D(tip.x, tip.y, Color(255, 255, 255))};
      renderer.fill_triangles(marker, 3, kNoTexture);
    }
  }

  session.game.hud(world, client.player_id(), s.game_hud, sizeof(s.game_hud));
  if (options.headless && options.hot_game && std::strcmp(s.game_hud, s.last_game_hud) != 0) {
    // A headless hot-reload session has no screen: print the game's HUD
    // text whenever it changes, so the effect of a reload can be seen.
    std::printf("tick %u: %s\n", static_cast<unsigned>(world.tick), s.game_hud);
    std::fflush(stdout);
    std::memcpy(s.last_game_hud, s.game_hud, sizeof(s.game_hud));
  }
  int players = 0;
  for (const Entity& entity : world.entities) {
    if (entity.alive && entity.player != kNoPlayer) ++players;
  }
  // A dark strip behind the HUD keeps the text readable over bright art.
  RectF hud_strip;
  hud_strip.w = static_cast<float>(renderer.output_width());
  hud_strip.h = now < s.notice_until ? 66.0f : 52.0f;
  renderer.fill_rect(hud_strip, Color(0, 0, 0, 150));
  char line[256];
  std::snprintf(line, sizeof(line), "Scraps  %s  scene %s  view %s  tick %u  %.0f fps",
                session.server ? "offline" : options.connect.c_str(),
                client.scene_name().empty() ? "-" : client.scene_name().c_str(), s.view ? s.view->name() : "-",
                static_cast<unsigned>(world.tick), s.fps);
  renderer.draw_debug_text(10.0f, 10.0f, line, Color(240, 240, 240));
  std::snprintf(line, sizeof(line), "%s  players %d  %s  rtt %.0f ms  unacked inputs %u",
                client_state_name(client.state()), players, s.game_hud, client.round_trip_seconds() * 1000.0,
                static_cast<unsigned>(client.unacknowledged_inputs()));
  renderer.draw_debug_text(10.0f, 24.0f, line, Color(200, 200, 210));
  renderer.draw_debug_text(10.0f, 38.0f,
                           "WASD/stick move  Space/south jump  E/west drop coin  Click place coin  V view  "
                           "-/= wheel zoom  P/start pause  F5 save  F9 load  Esc quit",
                           Color(150, 150, 165));
  if (now < s.notice_until) renderer.draw_debug_text(10.0f, 52.0f, s.notice.c_str(), Color(255, 220, 120));
  if (s.pause.paused() && !s.ui_present) {
    // Without a UI there is no pause menu: say how to resume.
    const char* text = "Paused - P to resume";
    const float text_width = static_cast<float>(std::strlen(text)) * 8.0f;
    const float x = (static_cast<float>(renderer.output_width()) - text_width) * 0.5f;
    const float y = static_cast<float>(renderer.output_height()) * 0.5f;
    RectF strip;
    strip.x = x - 12.0f;
    strip.y = y - 10.0f;
    strip.w = text_width + 24.0f;
    strip.h = 28.0f;
    renderer.fill_rect(strip, Color(0, 0, 0, 170));
    renderer.draw_debug_text(x, y, text, Color(255, 255, 255));
  }
  renderer.end_frame();

  // The UI hears about what changed (nothing is posted when nothing did).
  if (s.ui_present) {
    const char* notice = now < s.notice_until ? s.notice.c_str() : "";
    std::size_t length = std::strlen(notice);
    if (length > kUiNoticeBytes) length = kUiNoticeBytes;
    std::memcpy(s.ui_notice, notice, length);
    s.ui_notice[length] = '\0';
    UiAppState state;
    state.game = session.game.name();
    state.hud = s.game_hud;
    state.notice = s.ui_notice;
    state.has_save = s.has_save;
    s.ui->publish(*platform.ui, state, session.game);
  }

  // The player's volumes; music ducks to 40% of its volume while paused
  // (spec part 4). The mixer glides to the target.
  s.ui->apply_volumes(s.mixer);

  // Keep the audio device about 60 ms ahead.
  const std::size_t queued = platform.audio->queued_frames();
  if (queued < State::kAudioAheadFrames) {
    const std::size_t frames = State::kAudioAheadFrames - queued;
    s.mixer.mix(s.mix_buffer.data(), frames);
    platform.audio->submit(s.mix_buffer.data(), frames);
  }

  ++s.stats.frames;
  ++s.fps_frames;
  if (now - s.fps_window_start >= 0.5) {
    s.fps = static_cast<double>(s.fps_frames) / (now - s.fps_window_start);
    s.fps_frames = 0;
    s.fps_window_start = now;
  }
  return true;
}

int ClientApp::finish() {
  State& s = *m;
  if (s.failed) return 1;
  Client& client = *s.session.client;
  client.disconnect();
  if (s.session.server) s.session.server->poll(s.now);  // let the embedded server see the Bye

  if (s.options.headless) {
    const World& world = client.current_world();
    const Entity* me = find_entity(world, client.local_entity());
    std::printf("headless run: %ld frames, %ld ticks, world tick %u, client %s\n", s.stats.frames, s.stats.ticks,
                static_cast<unsigned>(world.tick), client_state_name(client.state()));
    if (me == nullptr) {
      std::printf("FAILED: the local player never appeared\n");
      return 1;
    }
    if (s.session.server) {
      std::printf("offline: saves %ld, loads %ld, save errors %ld, paused frames %ld, ticks while paused %ld\n",
                  s.stats.saves, s.stats.loads, s.stats.save_errors, s.stats.paused_frames,
                  s.stats.ticks_while_paused);
    }
    char hud[64] = {};
    s.session.game.hud(world, client.player_id(), hud, sizeof(hud));
    std::printf(
        "player at (%.2f, %.2f, %.2f), %s, entities %lu, images %lu, sounds %lu (%lu played), threads %u\n",
        me->position.x, me->position.y, me->position.z, hud, static_cast<unsigned long>(count_alive(world)),
        static_cast<unsigned long>(s.sprites.loaded_count()), static_cast<unsigned long>(s.mixer.sound_count()),
        static_cast<unsigned long>(s.sounds_played), s.session.jobs->worker_count());
    const MusicStream* playing = s.mixer.current_music();
    if (playing == nullptr) {
      std::printf("music: none\n");
    } else {
      std::printf("music: %s (%s, wrapped to its start %u times)\n", s.music.path().c_str(),
                  audio_format_name(playing->format()), static_cast<unsigned>(playing->loops()));
    }
  }
  return 0;
}

const ClientAppStats& ClientApp::stats() const { return m->stats; }
bool ClientApp::has_ui() const { return m->ui_present; }
const UiModel& ClientApp::ui() const { return *m->ui; }
const Client& ClientApp::client() const { return *m->session.client; }
const Mixer& ClientApp::mixer() const { return m->mixer; }
bool ClientApp::paused() const { return m->pause.paused(); }
const char* ClientApp::view_name() const { return m->view ? m->view->name() : "-"; }

}  // namespace scraps_apps
