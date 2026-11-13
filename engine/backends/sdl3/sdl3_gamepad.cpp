#include "backends/sdl3/sdl3_gamepad.h"

#include <SDL3/SDL.h>

namespace scraps::sdl3 {
namespace {

// SDL's gamepad buttons are positional too (SOUTH is the bottom face
// button), so the names map one to one. Buttons the engine does not know
// (guide, paddles, touchpad, misc) are ignored.
bool to_pad_button(Uint8 sdl_button, std::size_t& slot) {
  GamepadButton button = GamepadButton::kCount;
  switch (static_cast<SDL_GamepadButton>(sdl_button)) {
    case SDL_GAMEPAD_BUTTON_SOUTH: button = GamepadButton::kSouth; break;
    case SDL_GAMEPAD_BUTTON_EAST: button = GamepadButton::kEast; break;
    case SDL_GAMEPAD_BUTTON_WEST: button = GamepadButton::kWest; break;
    case SDL_GAMEPAD_BUTTON_NORTH: button = GamepadButton::kNorth; break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: button = GamepadButton::kDpadUp; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: button = GamepadButton::kDpadDown; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: button = GamepadButton::kDpadLeft; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: button = GamepadButton::kDpadRight; break;
    case SDL_GAMEPAD_BUTTON_START: button = GamepadButton::kStart; break;
    case SDL_GAMEPAD_BUTTON_BACK: button = GamepadButton::kBack; break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: button = GamepadButton::kLeftShoulder; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: button = GamepadButton::kRightShoulder; break;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: button = GamepadButton::kLeftStick; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: button = GamepadButton::kRightStick; break;
    default: return false;
  }
  slot = static_cast<std::size_t>(button);
  return true;
}

SDL_GamepadButton to_sdl_pad_button(GamepadButton button) {
  switch (button) {
    case GamepadButton::kSouth: return SDL_GAMEPAD_BUTTON_SOUTH;
    case GamepadButton::kEast: return SDL_GAMEPAD_BUTTON_EAST;
    case GamepadButton::kWest: return SDL_GAMEPAD_BUTTON_WEST;
    case GamepadButton::kNorth: return SDL_GAMEPAD_BUTTON_NORTH;
    case GamepadButton::kDpadUp: return SDL_GAMEPAD_BUTTON_DPAD_UP;
    case GamepadButton::kDpadDown: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    case GamepadButton::kDpadLeft: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    case GamepadButton::kDpadRight: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    case GamepadButton::kStart: return SDL_GAMEPAD_BUTTON_START;
    case GamepadButton::kBack: return SDL_GAMEPAD_BUTTON_BACK;
    case GamepadButton::kLeftShoulder: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    case GamepadButton::kRightShoulder: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    case GamepadButton::kLeftStick: return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    case GamepadButton::kRightStick: return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    default: return SDL_GAMEPAD_BUTTON_INVALID;
  }
}

// GamepadAxis is in SDL's order (left x/y, right x/y, left/right trigger);
// anything SDL may add later is ignored.
bool to_pad_axis(Uint8 sdl_axis, std::size_t& slot) {
  if (sdl_axis >= static_cast<Uint8>(GamepadAxis::kCount)) return false;
  slot = sdl_axis;
  return true;
}

// Sticks are -32768..32767, triggers 0..32767: both become -1..1 (0..1).
float axis_value(Sint16 value) {
  const float v = static_cast<float>(value) / 32767.0f;
  return v < -1.0f ? -1.0f : v;
}

}  // namespace

void GamepadReader::begin_frame() {
  m_pressed.fill(false);
  m_released.fill(false);
}

bool GamepadReader::handle(const SDL_Event& event, InputEventList& events) {
  std::size_t slot = 0;
  InputEvent item;
  switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
      if (m_pad == nullptr) open_any();
      return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
      if (m_pad != nullptr && event.gdevice.which == m_pad_id) {
        close();
        open_any();  // another pad that is still plugged in takes over
      }
      return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      if (m_pad != nullptr && event.gbutton.which == m_pad_id && to_pad_button(event.gbutton.button, slot)) {
        if (event.gbutton.down) {
          if (!m_down[slot]) {
            m_pressed[slot] = true;
            item.type = InputEventType::kPadDown;
            item.pad = static_cast<GamepadButton>(slot);
            events.push(item);
          }
        } else if (m_down[slot]) {
          m_released[slot] = true;
          item.type = InputEventType::kPadUp;
          item.pad = static_cast<GamepadButton>(slot);
          events.push(item);
        }
        m_down[slot] = event.gbutton.down;
      }
      return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      if (m_pad != nullptr && event.gaxis.which == m_pad_id && to_pad_axis(event.gaxis.axis, slot)) {
        m_axes[slot] = axis_value(event.gaxis.value);
      }
      return true;
    default:
      return false;
  }
}

bool GamepadReader::open_any() {
  if (m_pad != nullptr) return true;
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);  // allocates; only when a pad comes or goes
  if (ids == nullptr) return false;
  for (int i = 0; i < count && m_pad == nullptr; ++i) {
    m_pad = SDL_OpenGamepad(ids[i]);
    if (m_pad != nullptr) m_pad_id = ids[i];
  }
  SDL_free(ids);
  clear_state();
  read_state();
  return m_pad != nullptr;
}

void GamepadReader::close() {
  if (m_pad != nullptr) SDL_CloseGamepad(m_pad);
  m_pad = nullptr;
  m_pad_id = 0;
  clear_state();
}

void GamepadReader::clear_state() {
  m_down.fill(false);
  m_pressed.fill(false);
  m_released.fill(false);
  m_axes.fill(0.0f);
}

void GamepadReader::read_state() {
  if (m_pad == nullptr) return;
  for (std::size_t i = 0; i < kPadButtons; ++i) {
    m_down[i] = SDL_GetGamepadButton(m_pad, to_sdl_pad_button(static_cast<GamepadButton>(i)));
  }
  for (std::size_t i = 0; i < kPadAxes; ++i) {
    m_axes[i] = axis_value(SDL_GetGamepadAxis(m_pad, static_cast<SDL_GamepadAxis>(i)));
  }
}

bool GamepadReader::is_down(GamepadButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < m_down.size() && m_down[i];
}

bool GamepadReader::was_pressed(GamepadButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < m_pressed.size() && m_pressed[i];
}

bool GamepadReader::was_released(GamepadButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < m_released.size() && m_released[i];
}

float GamepadReader::axis(GamepadAxis axis) const {
  const std::size_t i = static_cast<std::size_t>(axis);
  return i < m_axes.size() ? m_axes[i] : 0.0f;
}

}  // namespace scraps::sdl3
