#pragma once

// The player's client as a one-frame function (docs/specs/2026-11-14-
// offline-essentials-and-ui.md, part 6a), so whoever owns the window loop can
// drive it: scraps_client's own loop (apps/client/main.cpp, SDL3 or headless)
// or a host whose event loop belongs to a WebView.
//
//   ClientApp app(options, platform, autopilot);
//   if (!app.start(error)) ...            // loads the game and the scene, starts the session
//   while (app.frame(now)) { }            // once per rendered frame
//   return app.finish();                  // says goodbye; the exit code
//
// frame() does what one turn of the old loop in main() did: pump the
// platform's events, poll the network, reload what changed, run the ticks
// the fixed-step clock asks for (Rule 2), draw, mix audio. Time comes in
// from the caller ("only the loop reads the clock").
//
// With a UI (platform.ui present, platform/ui.h) the app starts on the main
// menu and runs no ticks until the UI says "play" or "continue"; menus get
// the input and the game controls get none (apps/common/ui_model.h).
// Without one it behaves exactly as scraps_client always has.

#include <cstdint>
#include <memory>
#include <string>

#include "platform/services.h"

namespace scraps {
class Client;
class Mixer;
namespace headless {
class HeadlessInput;
}
}  // namespace scraps

namespace scraps_apps {

class UiModel;

struct ClientOptions {
  std::string scene;      // scene to play offline (empty: the game's default scene)
  std::string scene_dir;  // where .scene files are (resolved by the caller)
  std::string connect;    // empty: offline, with an embedded server
  std::string view;       // empty: the scene's own view
  bool headless = false;  // no window: the summary is printed by finish()
  long frames = 0;        // stop after this many frames (0: never)
  long threads = -1;      // worker threads for simulate; -1 = one less than the core count
  bool hot_game = false;  // load the game module and reload it when it is rebuilt
  // Where quick saves go; empty: the player's folder (SDL), or "saves/" headless.
  std::string save_dir;
  // The debug text at the top (scene, tick, fps, keys, notices). A host with
  // an HTML UI (scraps_web) turns it off unless asked (--debug-hud).
  bool debug_hud = true;
};

// Counts for the headless summary and for tests.
struct ClientAppStats {
  long frames = 0;
  long ticks = 0;
  long saves = 0;
  long loads = 0;
  long save_errors = 0;
  long paused_frames = 0;
  long ticks_while_paused = 0;
  long ui_commands = 0;    // commands the UI asked for, sent at a tick
  long game_commands = 0;  // commands other than Move sent at ticks (jumps, bindings, the UI's)
};

class ClientApp {
 public:
  // `platform` must outlive the app. `autopilot`: scripted input for
  // headless runs (it must be platform.input), or nullptr.
  ClientApp(const ClientOptions& options, scraps::PlatformServices platform,
            scraps::headless::HeadlessInput* autopilot);
  ~ClientApp();
  ClientApp(const ClientApp&) = delete;
  ClientApp& operator=(const ClientApp&) = delete;

  // Loads the game's code and the scene and starts the session. False, with
  // `error`, if it cannot; then do not call frame().
  bool start(std::string& error);

  // One frame at time `now` (seconds, from any fixed origin, never going
  // back). `wall_seconds` is real time, used only to look for a rebuilt game
  // module (--hot-game) when `now` is simulated; pass `now` otherwise.
  // Returns false when the app should stop: the window was closed, Esc or
  // "quit", the frame limit, or the connection failed (then finish() says 1).
  bool frame(double now, double wall_seconds);
  bool frame(double now) { return frame(now, now); }

  // Leaves the server and, headless, prints the summary. The exit code.
  int finish();

  // For hosts and tests.
  const ClientAppStats& stats() const;
  bool has_ui() const;
  const UiModel& ui() const;
  const scraps::Client& client() const;
  const scraps::Mixer& mixer() const;
  bool paused() const;
  // The view being drawn ("top-down", "side", "isometric"), or "-".
  const char* view_name() const;

 private:
  struct State;
  std::unique_ptr<State> m;
};

}  // namespace scraps_apps
