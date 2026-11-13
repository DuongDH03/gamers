#include "backends/webview2/batch_renderer.h"

#include <utility>

#include "backends/webview2/debug_font.h"
#include "backends/webview2/frame_format.h"
#include "platform/input.h"

namespace scraps::webview2 {
namespace {

constexpr std::uint32_t kOpTriangles = static_cast<std::uint32_t>(FrameOp::kTriangles);
constexpr std::uint32_t kOpLines = static_cast<std::uint32_t>(FrameOp::kLines);
// A single texture is at most a quarter of the largest slot, so it always
// fits in a frame with room for the draws.
constexpr std::size_t kMaxTextureBytes = kMaxSlotBytes / 4;

// The debug font as SDL lays it out (SDL_render.c, CreateDebugTextAtlas):
// 14 glyphs per row, each in a 10x10 cell with a 1-pixel border.
constexpr int kGlyphSize = 8;
constexpr int kCellSize = kGlyphSize + 2;
constexpr int kFontRows = kDebugFontGlyphs / kDebugFontGlyphsPerRow + 1;
constexpr int kFontWidth = kCellSize * kDebugFontGlyphsPerRow;
constexpr int kFontHeight = kCellSize * kFontRows;

// The next code point of UTF-8 `text` (advancing it), as SDL_StepUTF8 reads
// it: a byte that does not start a valid sequence is U+FFFD and one byte.
std::uint32_t step_utf8(const char*& text) {
  const unsigned char lead = static_cast<unsigned char>(text[0]);
  std::size_t available = 0;
  while (available < 4 && text[available] != '\0') ++available;
  const std::size_t length = InputEventList::utf8_sequence_length(text, available);
  if (length == 0) {
    ++text;
    return 0xFFFDu;
  }
  std::uint32_t code = 0;
  if (length == 1) {
    code = lead;
  } else if (length == 2) {
    code = lead & 0x1Fu;
  } else if (length == 3) {
    code = lead & 0x0Fu;
  } else {
    code = lead & 0x07u;
  }
  for (std::size_t i = 1; i < length; ++i) code = (code << 6) | (static_cast<unsigned char>(text[i]) & 0x3Fu);
  text += length;
  return code;
}

// The glyph index for a code point, or -1 for a blank (SDL_render.c,
// DrawDebugCharacter).
int glyph_for(std::uint32_t code) {
  if (code <= 32 || (code >= 127 && code <= 160)) return -1;
  if (code >= static_cast<std::uint32_t>(kDebugFontGlyphs)) return kDebugFontGlyphs - 1;
  if (code < 127) return static_cast<int>(code) - 33;
  return static_cast<int>(code) - 67;
}

}  // namespace

void BatchRenderer::set_output_size(int width, int height) {
  if (width > 0 && height > 0 && width <= static_cast<int>(kMaxTextureSide) &&
      height <= static_cast<int>(kMaxTextureSide)) {
    m_width = width;
    m_height = height;
  }
}

void BatchRenderer::queue_texture(TextureId id) {
  const Texture& texture = m_textures[id - 1];
  put_u32(m_resources, static_cast<std::uint32_t>(FrameOp::kTexture));
  put_u32(m_resources, id);
  put_u32(m_resources, texture.width);
  put_u32(m_resources, texture.height);
  m_resources.insert(m_resources.end(), texture.rgba.begin(), texture.rgba.end());
}

void BatchRenderer::resend_textures() {
  m_resources.clear();
  for (std::size_t i = 0; i < m_textures.size(); ++i) {
    const TextureId id = static_cast<TextureId>(i + 1);
    if (m_textures[i].alive) {
      queue_texture(id);
    } else {
      // A page that still has it (a new buffer, not a reload) frees it.
      put_u32(m_resources, static_cast<std::uint32_t>(FrameOp::kDestroyTexture));
      put_u32(m_resources, id);
    }
  }
}

TextureId BatchRenderer::create_texture(int width, int height, const std::uint8_t* rgba) {
  if (width <= 0 || height <= 0 || rgba == nullptr || width > static_cast<int>(kMaxTextureSide) ||
      height > static_cast<int>(kMaxTextureSide)) {
    return kNoTexture;
  }
  const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  if (bytes > kMaxTextureBytes) return kNoTexture;
  Texture texture;
  texture.width = static_cast<std::uint32_t>(width);
  texture.height = static_cast<std::uint32_t>(height);
  texture.rgba.assign(rgba, rgba + bytes);
  texture.alive = true;
  m_textures.push_back(std::move(texture));
  const TextureId id = static_cast<TextureId>(m_textures.size());
  queue_texture(id);
  return id;
}

void BatchRenderer::destroy_texture(TextureId texture) {
  if (texture == kNoTexture || texture > m_textures.size() || !m_textures[texture - 1].alive) return;
  Texture& slot = m_textures[texture - 1];
  slot.alive = false;
  std::vector<std::uint8_t>().swap(slot.rgba);
  if (texture == m_font) m_font = kNoTexture;
  put_u32(m_resources, static_cast<std::uint32_t>(FrameOp::kDestroyTexture));
  put_u32(m_resources, texture);
}

void BatchRenderer::begin_frame(Color clear_color) {
  m_recording = m_sink != nullptr && m_sink->can_take_frame();
  m_draws.clear();
  m_batch_op = 0;
  if (!m_recording) return;
  put_u32(m_draws, static_cast<std::uint32_t>(FrameOp::kClear));
  put_u32(m_draws, pack_rgba(clear_color));
}

bool BatchRenderer::open_batch(std::uint32_t op, TextureId texture, std::size_t vertices) {
  // Header, queued resources, the draws so far, this batch: all in one slot.
  const std::size_t needed = kFrameHeaderBytes + m_resources.size() + m_draws.size() + 12 + vertices * kVertexBytes;
  if (vertices > kMaxSlotBytes / kVertexBytes || needed > kMaxSlotBytes) return false;
  if (m_batch_op == op && m_batch_texture == texture &&
      m_batch_count <= 0xFFFFFFFFu - static_cast<std::uint32_t>(vertices)) {
    m_batch_count += static_cast<std::uint32_t>(vertices);
    store_u32(m_draws.data() + m_batch_count_at, m_batch_count);
    return true;
  }
  put_u32(m_draws, op);
  if (op == kOpTriangles) put_u32(m_draws, texture);
  m_batch_op = op;
  m_batch_texture = texture;
  m_batch_count_at = m_draws.size();
  m_batch_count = static_cast<std::uint32_t>(vertices);
  put_u32(m_draws, m_batch_count);
  return true;
}

void BatchRenderer::fill_triangles(const Vertex2D* vertices, std::size_t count, TextureId texture) {
  count -= count % 3;
  if (!m_recording || vertices == nullptr || count == 0) return;
  // As in sdl3: a texture that does not exist (or no longer) draws flat.
  if (texture != kNoTexture && (texture > m_textures.size() || !m_textures[texture - 1].alive)) texture = kNoTexture;
  if (!open_batch(kOpTriangles, texture, count)) return;
  for (std::size_t i = 0; i < count; ++i) put_vertex(m_draws, vertices[i]);
}

void BatchRenderer::fill_rect(const RectF& rect, Color color) {
  const Vertex2D quad[6] = {
      Vertex2D(rect.x, rect.y, color),          Vertex2D(rect.x + rect.w, rect.y, color),
      Vertex2D(rect.x + rect.w, rect.y + rect.h, color), Vertex2D(rect.x, rect.y, color),
      Vertex2D(rect.x + rect.w, rect.y + rect.h, color), Vertex2D(rect.x, rect.y + rect.h, color)};
  fill_triangles(quad, 6, kNoTexture);
}

void BatchRenderer::draw_line(float x0, float y0, float x1, float y1, Color color) {
  if (!m_recording || !open_batch(kOpLines, kNoTexture, 2)) return;
  put_vertex(m_draws, Vertex2D(x0, y0, color));
  put_vertex(m_draws, Vertex2D(x1, y1, color));
}

TextureId BatchRenderer::debug_font() {
  if (m_font != kNoTexture) return m_font;
  std::vector<std::uint8_t> atlas(static_cast<std::size_t>(kFontWidth) * kFontHeight * 4, 0);
  for (int glyph = 0; glyph < kDebugFontGlyphs; ++glyph) {
    const int left = (glyph % kDebugFontGlyphsPerRow) * kCellSize + 1;
    const int top = (glyph / kDebugFontGlyphsPerRow) * kCellSize + 1;
    for (int row = 0; row < kGlyphSize; ++row) {
      const std::uint8_t bits = kDebugFontData[glyph * 8 + row];
      for (int column = 0; column < kGlyphSize; ++column) {
        if ((bits & (1u << column)) == 0) continue;
        const std::size_t at = (static_cast<std::size_t>(top + row) * kFontWidth + (left + column)) * 4;
        atlas[at] = atlas[at + 1] = atlas[at + 2] = atlas[at + 3] = 255;
      }
    }
  }
  m_font = create_texture(kFontWidth, kFontHeight, atlas.data());
  return m_font;
}

void BatchRenderer::draw_debug_text(float x, float y, const char* text, Color color) {
  if (!m_recording || text == nullptr) return;
  const TextureId font = debug_font();  // once; then it is a texture like any other
  if (font == kNoTexture) return;
  float cursor = x;
  const char* at = text;
  while (*at != '\0') {
    const int glyph = glyph_for(step_utf8(at));
    if (glyph >= 0) {
      const float u0 = static_cast<float>((glyph % kDebugFontGlyphsPerRow) * kCellSize + 1) / kFontWidth;
      const float v0 = static_cast<float>((glyph / kDebugFontGlyphsPerRow) * kCellSize + 1) / kFontHeight;
      const float u1 = u0 + static_cast<float>(kGlyphSize) / kFontWidth;
      const float v1 = v0 + static_cast<float>(kGlyphSize) / kFontHeight;
      const float x1 = cursor + kGlyphSize;
      const float y1 = y + kGlyphSize;
      const Vertex2D quad[6] = {Vertex2D(cursor, y, color, u0, v0), Vertex2D(x1, y, color, u1, v0),
                                Vertex2D(x1, y1, color, u1, v1),     Vertex2D(cursor, y, color, u0, v0),
                                Vertex2D(x1, y1, color, u1, v1),     Vertex2D(cursor, y1, color, u0, v1)};
      fill_triangles(quad, 6, font);
    }
    cursor += kGlyphSize;
  }
}

void BatchRenderer::end_frame() {
  if (!m_recording) {
    ++m_frames_dropped;
    return;
  }
  m_recording = false;
  const std::size_t total = kFrameHeaderBytes + m_resources.size() + m_draws.size();
  m_frame.clear();
  if (total > kMaxSlotBytes) {
    ++m_frames_dropped;
    return;
  }
  put_u32(m_frame, kFrameMagic);
  put_u32(m_frame, kFormatVersion);
  put_u32(m_frame, m_next_seq);
  put_u32(m_frame, static_cast<std::uint32_t>(total));
  put_u32(m_frame, static_cast<std::uint32_t>(m_width));
  put_u32(m_frame, static_cast<std::uint32_t>(m_height));
  put_u32(m_frame, static_cast<std::uint32_t>(kFrameHeaderBytes + m_resources.size()));
  put_u32(m_frame, 0);
  m_frame.insert(m_frame.end(), m_resources.begin(), m_resources.end());
  m_frame.insert(m_frame.end(), m_draws.begin(), m_draws.end());
  if (m_sink->take_frame(m_frame.data(), m_frame.size(), m_next_seq)) {
    m_resources.clear();
    ++m_next_seq;
    ++m_frames_sent;
  } else {
    ++m_frames_dropped;
  }
}

}  // namespace scraps::webview2
