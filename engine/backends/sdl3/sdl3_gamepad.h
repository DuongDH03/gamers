#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "platform/input.h"

// SDL types are only forward-declared here (Rule 3: only .cpp files of this
// backend include SDL headers).
union SDL_Event;
struct SDL_Gamepad;

namespace scraps::sdl3 {

// The first connected gamepad, read from SDL events: buttons by position
// (south, east...), axes in -1..1 (triggers 0..1), presses and releases per
// frame. Shared by the windowed backend (Sdl3Platform) and the window-less
// devices service (Sdl3Devices) that the WebView2 app pairs with its window.
// Needs SDL_INIT_GAMEPAD.
class GamepadReader {
 public:
  GamepadReader() = default;
  ~GamepadReader() { close(); }
  GamepadReader(const GamepadReader&) = delete;
  GamepadReader& operator=(const GamepadReader&) = delete;

  // A new frame: forgets last frame's presses and releases.
  void begin_frame();
  // Handles `event` if it is a gamepad event (a pad added or removed, a
  // button, an axis), adding button events to `events`. True if it was one.
  bool handle(const SDL_Event& event, InputEventList& events);
  // Opens the first connected gamepad if none is open. True if one is open afterwards.
  bool open_any();
  void close();
  // Lets go of every button and centres the axes (focus lost: SDL sends no
  // pad events to a window in the background).
  void clear_state();
  // Takes the open pad's current buttons and axes, without "pressed" for
  // what is already held (focus gained).
  void read_state();

  bool connected() const { return m_pad != nullptr; }
  bool is_down(GamepadButton button) const;
  bool was_pressed(GamepadButton button) const;
  bool was_released(GamepadButton button) const;
  float axis(GamepadAxis axis) const;

 private:
  static constexpr std::size_t kPadButtons = static_cast<std::size_t>(GamepadButton::kCount);
  static constexpr std::size_t kPadAxes = static_cast<std::size_t>(GamepadAxis::kCount);
  SDL_Gamepad* m_pad = nullptr;
  std::uint32_t m_pad_id = 0;  // its SDL_JoystickID
  std::array<bool, kPadButtons> m_down{};
  std::array<bool, kPadButtons> m_pressed{};
  std::array<bool, kPadButtons> m_released{};
  std::array<float, kPadAxes> m_axes{};
};

}  // namespace scraps::sdl3
