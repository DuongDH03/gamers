#pragma once

#include <cstdint>

namespace scraps {

// The keys the engine knows about. Backends translate their own key codes.
enum class Key : std::uint8_t {
  kUnknown = 0,
  kW, kA, kS, kD, kE, kQ, kR, kV, kX, kZ,
  kUp, kDown, kLeft, kRight,
  kSpace, kEnter, kEscape, kTab,
  kMinus, kEquals,
  k1, k2, k3,
  kF1, kF2, kF3,
  kP, kF5, kF9,
  kCount
};

// Mouse buttons the engine knows about.
enum class MouseButton : std::uint8_t { kLeft, kRight, kMiddle, kCount };

// A point on screen in renderer output pixels. The platform layer has no Vec2
// (that lives in core), so it has its own small type.
struct PointerPosition {
  float x = 0.0f;
  float y = 0.0f;
};

// Keyboard and mouse state for the current rendered frame. Updated by
// Window::pump_events(). Polled, not an event list: two clicks inside one
// frame count as one, which is fine for a game.
class Input {
 public:
  virtual ~Input() = default;

  // True while the key is held.
  virtual bool is_down(Key key) const = 0;
  // True only in the frame in which the key went down.
  virtual bool was_pressed(Key key) const = 0;

  // Pointer position in renderer output pixels (the same space as
  // Renderer2D::output_width/height and View::world_to_screen), top-left
  // origin. Not window points: on a high-DPI screen those differ.
  virtual PointerPosition pointer() const = 0;
  // True while the pointer is over the window.
  virtual bool pointer_inside() const = 0;
  // True while the button is held.
  virtual bool is_down(MouseButton button) const = 0;
  // True only in the frame in which the button went down.
  virtual bool was_pressed(MouseButton button) const = 0;
  // True only in the frame in which the button went up.
  virtual bool was_released(MouseButton button) const = 0;
  // Wheel notches this frame; positive = away from the user.
  virtual float wheel() const = 0;
};

}  // namespace scraps
