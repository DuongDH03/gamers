#pragma once

#include <array>
#include <cstddef>

#include "core/command.h"
#include "core/movement.h"
#include "platform/input.h"
#include "views/view.h"

namespace scraps {

// Turns keyboard and mouse state into Commands (stage 3: input as commands).
//
// Input is read once per rendered frame, but commands are made once per
// simulation tick, and there can be zero or several ticks per frame. So key
// presses and clicks are latched in sample() and consumed in emit(): a quick
// tap is never lost, and never applied twice.
//
// A click is turned into a world point by the View when it is sampled (the
// camera of that frame is the one the player saw), and that point travels in
// the command's `vec`. It is only a request: the game's simulate decides
// whether it is allowed (Rule 5).
//
// Default bindings: WASD / arrow keys move, Space jumps. In a view without
// y movement (side view), W / Up also jump.
class Controls {
 public:
  static constexpr std::size_t kMaxBindings = 12;

  // Sends a command of `type` once each time `key` is pressed. Returns false
  // when all binding slots are used.
  bool bind(Key key, CommandType type);
  // Sends a command of `type` once each time `button` is clicked, with the
  // world point under the pointer in `vec`. Shares the slots with keys.
  bool bind(MouseButton button, CommandType type);
  // Removes every key and mouse binding (the movement keys stay), including
  // presses latched for them. Used when a game's code is swapped.
  void clear_bindings();

  // Call once per rendered frame, after Window::pump_events(). Clicks are
  // turned into world points with `view` and `camera` on the view's pointer
  // plane through `through` (View::screen_to_world).
  void sample(const Input& input, const View& view, const Camera& camera, const Vec3& through);
  // Keys only: mouse bindings are ignored (there is no view to pick with).
  void sample(const Input& input);

  // Call once per tick. Appends a Move command (always, so releasing the keys
  // stops the player), plus Jump and bound commands that were pressed since
  // the last emit. `rules` are the world's rules, `view` maps screen
  // directions to world directions.
  void emit(const View& view, const MovementRules& rules, PlayerId player, CommandList& out);

  // True if the last emit() produced a Jump command.
  bool jumped() const { return m_jumped; }

 private:
  struct Binding {
    Key key = Key::kUnknown;
    MouseButton button = MouseButton::kCount;  // kCount: a key binding
    CommandType type = CommandType::kNone;
    bool latched = false;
    Vec3 point;  // mouse bindings: the world point of the latched click
  };

  bool add_binding(Key key, MouseButton button, CommandType type);

  Vec2 m_screen_direction;
  bool m_jump_latched = false;
  bool m_up_latched = false;
  bool m_jumped = false;
  std::array<Binding, kMaxBindings> m_bindings{};
  std::size_t m_binding_count = 0;
};

}  // namespace scraps
