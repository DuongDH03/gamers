#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/color.h"

namespace scraps {

// One corner of a filled triangle, in screen pixels (x right, y down).
// `u`, `v` are texture coordinates in 0..1 (ignored without a texture);
// `color` tints the texture (white = unchanged) or is the fill colour.
struct Vertex2D {
  float x = 0.0f;
  float y = 0.0f;
  Color color;
  float u = 0.0f;
  float v = 0.0f;

  constexpr Vertex2D() = default;
  constexpr Vertex2D(float x_value, float y_value, Color color_value, float u_value = 0.0f, float v_value = 0.0f)
      : x(x_value), y(y_value), color(color_value), u(u_value), v(v_value) {}
};

struct RectF {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

// A texture owned by the renderer. 0 means "no texture" (flat colour).
using TextureId = std::uint32_t;
constexpr TextureId kNoTexture = 0;

// Size in pixels of one glyph of the built-in debug font.
constexpr int kDebugGlyphSize = 8;

// Draws 2D shapes in screen space. Knows nothing about worlds, cameras or
// views: a View turns the world into these calls (Rule 4).
class Renderer2D {
 public:
  virtual ~Renderer2D() = default;

  // Uploads an image: `rgba` is width * height * 4 bytes, rows top to bottom.
  // Returns kNoTexture on failure. Pixel art is sampled without smoothing.
  virtual TextureId create_texture(int width, int height, const std::uint8_t* rgba) = 0;
  virtual void destroy_texture(TextureId texture) = 0;

  virtual void begin_frame(Color clear_color) = 0;
  virtual void fill_rect(const RectF& rect, Color color) = 0;
  virtual void draw_line(float x0, float y0, float x1, float y1, Color color) = 0;
  // `count` must be a multiple of 3; every three vertices form one triangle.
  // Triangles are drawn in order, so later ones cover earlier ones. With a
  // texture, each vertex's (u, v) picks the texel and its colour tints it.
  virtual void fill_triangles(const Vertex2D* vertices, std::size_t count, TextureId texture) = 0;
  virtual void draw_debug_text(float x, float y, const char* text, Color color) = 0;
  virtual void end_frame() = 0;

  virtual int output_width() const = 0;
  virtual int output_height() const = 0;
};

}  // namespace scraps
