#pragma once

#include <array>
#include <cstddef>

#include "core/command.h"
#include "core/movement.h"
#include "platform/input.h"
#include "views/view.h"

namespace scraps {

// Turns keyboard, mouse and gamepad state into Commands (stage 3: input as
// commands).
//
// Input is read once per rendered frame, but commands are made once per
// simulation tick, and there can be zero or several ticks per frame. So key
// presses, clicks and gamepad presses are latched in sample() and consumed
// in emit(): a quick tap is never lost, and never applied twice.
//
// A click is turned into a world point by the View when it is sampled (the
// camera of that frame is the one the player saw), and that point travels in
// the command's `vec`. It is only a request: the game's simulate decides
// whether it is allowed (Rule 5).
//
// Default bindings: WASD / arrow keys / d-pad move, Space / gamepad south
// jump. In a view without y movement (side view), W / Up / d-pad up also
// jump. The left stick moves too, analog: through the View like the keys
// (Rule 4), after a radial dead zone, so Move's vec is shorter than 1 when
// the stick is not pushed all the way. When a movement key or the d-pad is
// held, it wins over the stick.
class Controls {
 public:
  static constexpr std::size_t kMaxBindings = 12;
  // Stick deflections up to this length count as resting (worn sticks do not
  // centre exactly). Beyond it, output starts again from 0, so there is no
  // jump in speed at the edge.
  static constexpr float kStickDeadZone = 0.2f;

  // Sends a command of `type` once each time `key` is pressed. Returns false
  // when all binding slots are used.
  bool bind(Key key, CommandType type);
  // Sends a command of `type` once each time `button` is clicked, with the
  // world point under the pointer in `vec`. Shares the slots with keys.
  bool bind(MouseButton button, CommandType type);
  // Sends a command of `type` once each time the gamepad `button` is pressed.
  // Shares the slots with keys and mouse buttons.
  bool bind(GamepadButton button, CommandType type);
  // Removes every key, mouse and gamepad binding (the movement controls
  // stay), including presses latched for them. Used when a game's code is
  // swapped.
  void clear_bindings();
  // Forgets presses and clicks latched but not yet emitted (bindings stay).
  // Called when the game resumes or starts: a press made while no ticks ran
  // (paused, or in a menu) must not act afterwards.
  void clear_latches();

  // Call once per rendered frame, after Window::pump_events(). Clicks are
  // turned into world points with `view` and `camera` on the view's pointer
  // plane through `through` (View::screen_to_world).
  void sample(const Input& input, const View& view, const Camera& camera, const Vec3& through);
  // Keys and gamepad only: mouse bindings are ignored (there is no view to
  // pick with).
  void sample(const Input& input);

  // Call once per tick. Appends a Move command (always, so releasing the keys
  // stops the player), plus Jump and bound commands that were pressed since
  // the last emit. `rules` are the world's rules, `view` maps screen
  // directions to world directions.
  void emit(const View& view, const MovementRules& rules, PlayerId player, CommandList& out);

  // True if the last emit() produced a Jump command.
  bool jumped() const { return m_jumped; }

  // A raw stick position (screen directions: x right, y down) with the
  // radial dead zone applied and rescaled: length 0 up to kStickDeadZone,
  // then rising to 1 at full deflection. Never longer than 1; zero if not
  // finite.
  static Vec2 apply_dead_zone(Vec2 stick);

 private:
  struct Binding {
    Key key = Key::kUnknown;
    MouseButton button = MouseButton::kCount;  // kCount: not a mouse binding
    GamepadButton pad = GamepadButton::kCount;  // kCount: not a gamepad binding
    CommandType type = CommandType::kNone;
    bool latched = false;
    Vec3 point;  // mouse bindings: the world point of the latched click
  };

  bool add_binding(Key key, MouseButton button, GamepadButton pad, CommandType type);
  // The stick's ground direction in `view`, as long as the stick is pushed.
  static Vec2 stick_to_ground(const View& view, Vec2 stick);

  Vec2 m_screen_direction;  // keys and d-pad: each component -1, 0 or 1
  Vec2 m_stick;             // left stick after the dead zone, length <= 1
  bool m_jump_latched = false;
  bool m_up_latched = false;
  bool m_jumped = false;
  std::array<Binding, kMaxBindings> m_bindings{};
  std::size_t m_binding_count = 0;
};

}  // namespace scraps
