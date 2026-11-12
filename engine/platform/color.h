#pragma once

#include <cstdint>

namespace scraps {

// An 8-bit-per-channel RGBA color. Plain data: safe to copy and serialize.
struct Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  constexpr Color() = default;
  constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 255)
      : r(red), g(green), b(blue), a(alpha) {}
};

inline bool operator==(Color lhs, Color rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}
inline bool operator!=(Color lhs, Color rhs) { return !(lhs == rhs); }

// Multiplies the RGB channels by `factor` (clamped to 0..255). Alpha is kept.
inline Color scale_rgb(Color color, float factor) {
  auto channel = [factor](std::uint8_t value) {
    float scaled = static_cast<float>(value) * factor;
    if (scaled < 0.0f) scaled = 0.0f;
    if (scaled > 255.0f) scaled = 255.0f;
    return static_cast<std::uint8_t>(scaled);
  };
  return Color(channel(color.r), channel(color.g), channel(color.b), color.a);
}

}  // namespace scraps
