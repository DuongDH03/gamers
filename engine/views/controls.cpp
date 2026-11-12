#include "views/controls.h"

namespace scraps {

bool Controls::add_binding(Key key, MouseButton button, CommandType type) {
  if (m_binding_count >= kMaxBindings) return false;
  Binding& binding = m_bindings[m_binding_count++];
  binding = Binding();
  binding.key = key;
  binding.button = button;
  binding.type = type;
  return true;
}

bool Controls::bind(Key key, CommandType type) { return add_binding(key, MouseButton::kCount, type); }

bool Controls::bind(MouseButton button, CommandType type) {
  if (button == MouseButton::kCount) return false;
  return add_binding(Key::kUnknown, button, type);
}

void Controls::clear_bindings() {
  m_bindings.fill(Binding());
  m_binding_count = 0;
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
  float x = 0.0f;
  float y = 0.0f;
  if (input.is_down(Key::kA) || input.is_down(Key::kLeft)) x -= 1.0f;
  if (input.is_down(Key::kD) || input.is_down(Key::kRight)) x += 1.0f;
  if (input.is_down(Key::kW) || input.is_down(Key::kUp)) y -= 1.0f;
  if (input.is_down(Key::kS) || input.is_down(Key::kDown)) y += 1.0f;
  m_screen_direction = Vec2(x, y);

  if (input.was_pressed(Key::kSpace)) m_jump_latched = true;
  if (input.was_pressed(Key::kW) || input.was_pressed(Key::kUp)) m_up_latched = true;
  for (std::size_t i = 0; i < m_binding_count; ++i) {
    Binding& binding = m_bindings[i];
    if (binding.button == MouseButton::kCount && input.was_pressed(binding.key)) binding.latched = true;
  }
}

void Controls::emit(const View& view, const MovementRules& rules, PlayerId player, CommandList& out) {
  const Vec2 ground = view.screen_to_ground_direction(m_screen_direction);
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
