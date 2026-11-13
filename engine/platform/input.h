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

// Gamepad buttons, named by position, not by label: kSouth is the bottom face
// button (A on Xbox, Cross on PlayStation, B on Nintendo), kEast the right
// one, and so on. A game binds "south" and every pad agrees where that is.
enum class GamepadButton : std::uint8_t {
  kSouth, kEast, kWest, kNorth,
  kDpadUp, kDpadDown, kDpadLeft, kDpadRight,
  kStart, kBack,
  kLeftShoulder, kRightShoulder,
  kLeftStick, kRightStick,  // pressing a stick in
  kCount
};

// Gamepad axes. Sticks are -1..1 with screen conventions: x right is +1, y
// down is +1 (like SDL and like screen pixels). Triggers are 0..1. Values are
// raw: no dead zone, because what counts as "resting" is a gameplay choice
// (Controls applies one).
enum class GamepadAxis : std::uint8_t {
  kLeftX, kLeftY, kRightX, kRightY,
  kLeftTrigger, kRightTrigger,
  kCount
};

// A point on screen in renderer output pixels. The platform layer has no Vec2
// (that lives in core), so it has its own small type.
struct PointerPosition {
  float x = 0.0f;
  float y = 0.0f;
};

// Keyboard, mouse and gamepad state for the current rendered frame. Updated
// by Window::pump_events(). Polled, not an event list: two clicks inside one
// frame count as one, which is fine for a game.
//
// Gamepad: only the first connected pad is read (local multiplayer with
// several pads is out of scope). While none is connected, every button is up
// and every axis is 0.
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

  // True while a gamepad is connected and being read.
  virtual bool gamepad_connected() const = 0;
  // True while the button is held.
  virtual bool is_down(GamepadButton button) const = 0;
  // True only in the frame in which the button went down. A tap shorter than
  // a frame is seen as pressed and released in the same frame.
  virtual bool was_pressed(GamepadButton button) const = 0;
  // True only in the frame in which the button went up.
  virtual bool was_released(GamepadButton button) const = 0;
  // The axis now, raw (see GamepadAxis): -1..1 for sticks, 0..1 for triggers.
  virtual float axis(GamepadAxis axis) const = 0;
};

}  // namespace scraps
