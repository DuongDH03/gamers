#pragma once

#include <cmath>

namespace scraps {

constexpr float kPi = 3.14159265358979f;

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  constexpr Vec2() = default;
  constexpr Vec2(float x_value, float y_value) : x(x_value), y(y_value) {}
};

// A point or direction in world units. `z` is height above the ground plane
// (Rule 4); which way that looks on screen is decided by a View.
struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  constexpr Vec3() = default;
  constexpr Vec3(float x_value, float y_value, float z_value) : x(x_value), y(y_value), z(z_value) {}
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return Vec2(a.x + b.x, a.y + b.y); }
inline Vec2 operator-(Vec2 a, Vec2 b) { return Vec2(a.x - b.x, a.y - b.y); }
inline Vec2 operator*(Vec2 a, float s) { return Vec2(a.x * s, a.y * s); }
inline bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Vec2 a, Vec2 b) { return !(a == b); }

inline Vec3 operator+(Vec3 a, Vec3 b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(Vec3 a, Vec3 b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(Vec3 a, float s) { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3& operator+=(Vec3& a, Vec3 b) {
  a.x += b.x;
  a.y += b.y;
  a.z += b.z;
  return a;
}
inline bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool operator!=(Vec3 a, Vec3 b) { return !(a == b); }

inline float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

// Returns `v` scaled to length 1, or zero if `v` is (almost) zero.
inline Vec2 normalized(Vec2 v) {
  const float len = length(v);
  if (len < 1e-6f) return Vec2();
  return Vec2(v.x / len, v.y / len);
}

inline bool is_finite(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
inline bool is_finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) {
  return Vec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t));
}

}  // namespace scraps
