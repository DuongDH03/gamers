#include "views/controls.h"

namespace scraps {

bool Controls::add_binding(Key key, MouseButton button, GamepadButton pad, CommandType type) {
  if (m_binding_count >= kMaxBindings) return false;
  Binding& binding = m_bindings[m_binding_count++];
  binding = Binding();
  binding.key = key;
  binding.button = button;
  binding.pad = pad;
  binding.type = type;
  return true;
}

bool Controls::bind(Key key, CommandType type) {
  return add_binding(key, MouseButton::kCount, GamepadButton::kCount, type);
}

bool Controls::bind(MouseButton button, CommandType type) {
  if (button == MouseButton::kCount) return false;
  return add_binding(Key::kUnknown, button, GamepadButton::kCount, type);
}

bool Controls::bind(GamepadButton button, CommandType type) {
  if (button >= GamepadButton::kCount) return false;
  return add_binding(Key::kUnknown, MouseButton::kCount, button, type);
}

void Controls::clear_bindings() {
  m_bindings.fill(Binding());
  m_binding_count = 0;
}

Vec2 Controls::apply_dead_zone(Vec2 stick) {
  if (!is_finite(stick)) return Vec2();
  const float len = length(stick);
  if (!(len > kStickDeadZone)) return Vec2();
  // Radial, not per axis: a per-axis dead zone snaps slightly diagonal
  // pushes onto the axes. Rescaled so the output starts at 0 at the edge.
  float amount = (len - kStickDeadZone) / (1.0f - kStickDeadZone);
  if (amount > 1.0f) amount = 1.0f;  // a stick's corners can read past 1
  return stick * (amount / len);
}

void Controls::sample(const Input& input, const View& view, const Camera& camera, const Vec3& through) {
  sample(input);
  for (std::size_t i = 0; i < m_binding_count; ++i) {
    Binding& binding = m_bindings[i];
    if (binding.button == MouseButton::kCount || !input.was_pressed(binding.button)) continue;
    // The point is taken now, at press time: by the next tick the camera may
    // have moved, and the player clicked on what was on screen.
    const PointerPosition pointer = input.pointer();
    binding.point = view.screen_to_world(Vec2(pointer.x, pointer.y), camera, through);
    binding.latched = true;
  }
}

void Controls::sample(const Input& input) {
  const bool pad = input.gamepad_connected();
  auto held = [&input, pad](Key key, Key arrow, GamepadButton dpad) {
    return input.is_down(key) || input.is_down(arrow) || (pad && input.is_down(dpad));
  };
  auto pressed = [&input, pad](Key key, Key arrow, GamepadButton dpad) {
    return input.was_pressed(key) || input.was_pressed(arrow) || (pad && input.was_pressed(dpad));
  };

  float x = 0.0f;
  float y = 0.0f;
  if (held(Key::kA, Key::kLeft, GamepadButton::kDpadLeft)) x -= 1.0f;
  if (held(Key::kD, Key::kRight, GamepadButton::kDpadRight)) x += 1.0f;
  if (held(Key::kW, Key::kUp, GamepadButton::kDpadUp)) y -= 1.0f;
  if (held(Key::kS, Key::kDown, GamepadButton::kDpadDown)) y += 1.0f;
  m_screen_direction = Vec2(x, y);
  m_stick = pad ? apply_dead_zone(Vec2(input.axis(GamepadAxis::kLeftX), input.axis(GamepadAxis::kLeftY))) : Vec2();

  if (input.was_pressed(Key::kSpace) || (pad && input.was_pressed(GamepadButton::kSouth))) m_jump_latched = true;
  if (pressed(Key::kW, Key::kUp, GamepadButton::kDpadUp)) m_up_latched = true;
  for (std::size_t i = 0; i < m_binding_count; ++i) {
    Binding& binding = m_bindings[i];
    if (binding.pad != GamepadButton::kCount) {
      if (pad && input.was_pressed(binding.pad)) binding.latched = true;
    } else if (binding.button == MouseButton::kCount && input.was_pressed(binding.key)) {
      binding.latched = true;
    }
  }
}

Vec2 Controls::stick_to_ground(const View& view, Vec2 stick) {
  // Keep only the screen axes that mean something on this view's ground. In
  // the side view screen up/down is no ground direction, so a stick pushed
  // mostly up walks as slowly as its small sideways part says, instead of
  // at full speed. Asked of the View, so Controls never learns which way is
  // down (Rule 4).
  if (view.screen_to_ground_direction(Vec2(1.0f, 0.0f)) == Vec2()) stick.x = 0.0f;
  if (view.screen_to_ground_direction(Vec2(0.0f, 1.0f)) == Vec2()) stick.y = 0.0f;
  float amount = length(stick);
  if (!(amount > 0.0f)) return Vec2();
  if (amount > 1.0f) amount = 1.0f;
  return view.screen_to_ground_direction(stick) * amount;
}

void Controls::emit(const View& view, const MovementRules& rules, PlayerId player, CommandList& out) {
  const Vec2 ground = m_screen_direction != Vec2() ? view.screen_to_ground_direction(m_screen_direction)
                                                   : stick_to_ground(view, m_stick);
  out.push(make_command(CommandType::kMove, player, Vec3(ground.x, ground.y, 0.0f)));

  const bool up_jumps = !rules.move_y && rules.jump_speed > 0.0f;
  m_jumped = m_jump_latched || (up_jumps && m_up_latched);
  if (m_jumped) out.push(make_command(CommandType::kJump, player));
  m_jump_latched = false;
  m_up_latched = false;

  for (std::size_t i = 0; i < m_binding_count; ++i) {
    Binding& binding = m_bindings[i];
    if (!binding.latched) continue;
    out.push(make_command(binding.type, player, binding.point));
    binding.latched = false;
  }
}

}  // namespace scraps
