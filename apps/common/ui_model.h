#pragma once

// The app side of the UI bridge (docs/specs/2026-11-14-offline-essentials-
// and-ui.md, part 6a): what the UI is told ("view" messages) and what the
// app does with what it says ("action" messages). Shared by every client
// that has a UI; the transport is a platform UiHost (platform/ui.h).
//
//   UiHost::receive ─► parse_ui_action ─► UiModel::handle
//        ├─ UI-only: settings open/closed, volumes, menu open (online)
//        ├─ game commands: queued, taken by the app at its next tick as a
//        │  Command for the local player (Rule 1: the UI never edits the World)
//        └─ app requests (play, continue, quit, pause, resume, save, load):
//           returned in UiRequests; the app carries them out with the same
//           code as its keys (P, F5, F9, Esc), so there is one path for each.
//   app state ─► UiModel::publish ─► UiHost::post, only when the text changed
//
// Screens: "main" (not started yet: nothing ticks), "playing" (the game has
// the keyboard and mouse), "paused" (the pause menu; offline the game is
// paused, online it runs on, since one player cannot stop the server), and
// "settings". Outside "playing" the game controls get no input.
//
// Untrusted input: every message is parsed with fixed limits (ui_json.h);
// malformed and unknown ones are ignored and counted, never half-applied.

#include <array>
#include <cstddef>
#include <cstdint>

#include "common/game_module.h"
#include "common/pause.h"
#include "common/ui_json.h"
#include "core/command.h"
#include "core/mixer.h"
#include "platform/input.h"
#include "platform/ui.h"

namespace scraps_apps {

enum class UiScreen : std::uint8_t { kMain, kPlaying, kPaused, kSettings };
// "main", "playing", "paused", "settings": the names in the view message.
const char* ui_screen_name(UiScreen screen);

// The "name" of an action message.
enum class UiActionType : std::uint8_t {
  kPlay,
  kContinue,
  kQuit,
  kPause,
  kResume,
  kSave,
  kLoad,
  kOpenSettings,
  kCloseSettings,
  kSetMusicVolume,
  kSetEffectsVolume,
  kCommand,
};
// "play", "continue", ... "command": the names in the action message.
const char* ui_action_name(UiActionType type);

struct UiAction {
  UiActionType type = UiActionType::kPlay;
  float value = 0.0f;  // kSetMusicVolume, kSetEffectsVolume: 0..1 (clamped)
  std::array<char, scraps::kGameUiCommandNameCapacity> command{};  // kCommand: the game command's name
};

enum class UiParse : std::uint8_t {
  kAction,     // `out` holds a valid action
  kMalformed,  // not JSON, not flat, a field missing or of the wrong type
  kUnknown,    // well formed, but not an action this app knows
};

// Reads one UI → native message: {"type":"action","name":...} plus "value"
// (a number, for the volumes) or "command" (a string, for kCommand). Other
// fields are ignored. `scratch` is parse storage (no allocation).
UiParse parse_ui_action(const char* json, std::size_t size, JsonObject& scratch, UiAction& out);

// What the app must do for the UI, with its own code (see the top).
enum class UiRequest : std::uint8_t { kPlay, kContinue, kQuit, kPause, kResume, kSave, kLoad };

struct UiRequests {
  static constexpr std::size_t kCapacity = 16;
  std::array<UiRequest, kCapacity> items{};
  std::size_t count = 0;
  bool push(UiRequest request) {
    if (count >= kCapacity) return false;
    items[count++] = request;
    return true;
  }
};

// The app's side of the view, gathered each frame.
struct UiAppState {
  const char* game = "";    // the game's name
  const char* hud = "";     // the game's HUD text ("coins 3")
  const char* notice = "";  // the notice on screen now, or ""
  bool has_save = false;    // a quick save exists (offline)
};

// An Input that reports nothing: no key down, no click, no pointer, no pad,
// no events. What the game controls sample while they must not see input.
class NoInput final : public scraps::Input {
 public:
  bool is_down(scraps::Key) const override { return false; }
  bool was_pressed(scraps::Key) const override { return false; }
  scraps::PointerPosition pointer() const override { return scraps::PointerPosition(); }
  bool pointer_inside() const override { return false; }
  bool is_down(scraps::MouseButton) const override { return false; }
  bool was_pressed(scraps::MouseButton) const override { return false; }
  bool was_released(scraps::MouseButton) const override { return false; }
  float wheel() const override { return 0.0f; }
  bool gamepad_connected() const override { return false; }
  bool is_down(scraps::GamepadButton) const override { return false; }
  bool was_pressed(scraps::GamepadButton) const override { return false; }
  bool was_released(scraps::GamepadButton) const override { return false; }
  float axis(scraps::GamepadAxis) const override { return 0.0f; }
  const scraps::InputEventList& events() const override { return m_events; }
  void start_text_input() override {}
  void stop_text_input() override {}
  bool text_input_active() const override { return false; }

 private:
  scraps::InputEventList m_events;
};

class UiModel {
 public:
  static constexpr std::size_t kMaxQueuedCommands = 16;
  static constexpr std::size_t kViewCapacity = 4096;  // fits the longest view, every string escaped

  // `pause` is the app's (read live: offline, "paused" is its state).
  // `online`: no embedded server. `start_at_menu`: a UI is shown, so the
  // app starts on the main menu; without one it plays at once ("playing").
  UiModel(const OfflinePause& pause, bool online, bool start_at_menu);

  UiScreen screen() const;
  bool started() const { return m_started; }
  // The game controls get input only while playing.
  bool game_has_input() const { return screen() == UiScreen::kPlaying; }
  // The app calls this once "play" or "continue" has worked.
  void mark_started() { m_started = true; }

  // The player's volumes, 0..1 (1 until the UI sets them).
  float music_volume() const { return m_music_volume; }
  float effects_volume() const { return m_effects_volume; }
  // Sets the mixer's gains: music = volume × the pause duck (kPauseMusicDuck
  // while paused), effects = volume. Neither overwrites the other. Call
  // every frame; the mixer glides the music gain.
  void apply_volumes(scraps::Mixer& mixer) const;

  // Takes every message the UI sent since the last frame and handles it.
  // App requests go into `out` in order.
  void receive(scraps::UiHost& host, const GameModule& game, UiRequests& out);
  // One action (receive() calls it for each well-formed one).
  void handle(const UiAction& action, const GameModule& game, UiRequests& out);
  // Native keys while a UI is shown. Esc: playing → pause menu, pause menu →
  // resume, settings → back, main → nothing. P / gamepad start: playing ↔
  // pause menu only.
  void escape(UiRequests& out);
  void toggle_pause(UiRequests& out);

  // The UI's game commands for the next tick, for `player`, appended to
  // `out` in order (and forgotten). Call at a tick while connected.
  void take_commands(scraps::PlayerId player, scraps::CommandList& out);
  std::size_t queued_commands() const { return m_queued; }

  // Builds the view message and posts it if its text differs from the last
  // one posted. Returns true if it posted. Allocates nothing.
  bool publish(scraps::UiHost& host, const UiAppState& state, const GameModule& game);
  // Writes the view message into `out`; returns its size, 0 if it did not fit.
  std::size_t write_view(const UiAppState& state, const GameModule& game, char* out, std::size_t capacity) const;
  // The last view posted (not terminated), for tests.
  const char* last_view() const { return m_last_view.data(); }
  std::size_t last_view_size() const { return m_last_view_size; }

  // Counters: messages that were not JSON or not an action (malformed), well
  // formed but unknown (another type, action or command name), actions that
  // made no sense now (ignored: "play" while playing, a command in a menu),
  // and messages, requests or commands dropped for lack of room.
  std::size_t malformed() const { return m_malformed; }
  std::size_t unknown() const { return m_unknown; }
  std::size_t ignored() const { return m_ignored; }
  std::size_t dropped() const { return m_dropped; }
  std::size_t posts() const { return m_posts; }

 private:
  void request_pause(UiRequests& out);
  void request_resume(UiRequests& out);
  void push(UiRequests& out, UiRequest request);

  const OfflinePause& m_pause;
  bool m_online;
  bool m_started;
  bool m_menu_open = false;  // online only: the pause menu over a running game
  bool m_settings_open = false;
  float m_music_volume = 1.0f;
  float m_effects_volume = 1.0f;

  std::array<scraps::CommandType, kMaxQueuedCommands> m_commands{};
  std::size_t m_queued = 0;

  scraps::UiMessageList m_inbox;
  JsonObject m_json;
  std::array<char, kViewCapacity> m_view{};
  std::array<char, kViewCapacity> m_last_view{};
  std::size_t m_last_view_size = 0;
  bool m_posted_once = false;

  std::size_t m_malformed = 0;
  std::size_t m_unknown = 0;
  std::size_t m_ignored = 0;
  std::size_t m_dropped = 0;
  std::size_t m_posts = 0;
};

}  // namespace scraps_apps
