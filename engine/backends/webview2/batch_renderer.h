#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platform/renderer2d.h"

namespace scraps::webview2 {

// Where finished frames go: the WebView2 window, which copies them into a
// shared-buffer slot for the page (webview2_backend.cpp), or a test.
class FrameSink {
 public:
  virtual ~FrameSink() = default;
  // True if a frame begun now could be shown (the page is ready and a slot
  // is free). Otherwise the renderer records no draws this frame (the game
  // still runs: ticks, sound and network do not wait for the page).
  virtual bool can_take_frame() const = 0;
  // A finished frame, `size` bytes in the frame_format.h layout, with `seq`
  // in its header. True: it was handed to the page, resources included.
  virtual bool take_frame(const std::uint8_t* bytes, std::size_t size, std::uint32_t seq) = 0;
};

// Renderer2D for a page that draws with WebGL2 (UI layout A). It draws
// nothing itself: it records each frame's calls as frame_format.h commands,
// in painter's order, merging calls in a row that share a texture into one
// batch, and hands the bytes to a FrameSink at end_frame().
//
// Textures are uploaded once: create_texture() queues the pixels as a
// resource that travels with the next frame the page takes. A copy is kept,
// because a page that reloads (F5 in DevTools, Vite) has lost its textures:
// resend_textures() queues them all again.
//
// draw_debug_text uses SDL's 8x8 debug font (debug_font.h) as a texture of
// its own, laid out as SDL lays it out, so the text looks the same.
//
// Once the buffers have grown to the largest frame, recording allocates nothing.
class BatchRenderer final : public Renderer2D {
 public:
  explicit BatchRenderer(FrameSink* sink) : m_sink(sink) {}

  // The page's canvas size in pixels (CSS size times devicePixelRatio).
  void set_output_size(int width, int height);
  // Queues every live texture (and the ids of the destroyed ones) again, in
  // place of anything queued: the page lost what it had.
  void resend_textures();

  // Frames handed to the sink, and frames recorded but not taken.
  std::uint32_t frames_sent() const { return m_frames_sent; }
  std::uint32_t frames_dropped() const { return m_frames_dropped; }
  // The bytes of the last frame end_frame() built (sent or not), for tests.
  const std::vector<std::uint8_t>& last_frame() const { return m_frame; }
  // Resource bytes waiting for the next frame the page takes.
  std::size_t queued_resource_bytes() const { return m_resources.size(); }

  // Renderer2D
  TextureId create_texture(int width, int height, const std::uint8_t* rgba) override;
  void destroy_texture(TextureId texture) override;
  void begin_frame(Color clear_color) override;
  void fill_rect(const RectF& rect, Color color) override;
  void draw_line(float x0, float y0, float x1, float y1, Color color) override;
  void fill_triangles(const Vertex2D* vertices, std::size_t count, TextureId texture) override;
  void draw_debug_text(float x, float y, const char* text, Color color) override;
  void end_frame() override;
  int output_width() const override { return m_width; }
  int output_height() const override { return m_height; }

 private:
  struct Texture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;  // kept for resend_textures(); empty once destroyed
    bool alive = false;
  };

  // Makes room for `vertices` more vertices of a batch of `op` with
  // `texture`: extends the last batch if it matches, else starts one.
  // False if the frame would grow past what a slot can hold.
  bool open_batch(std::uint32_t op, TextureId texture, std::size_t vertices);
  void queue_texture(TextureId id);
  TextureId debug_font();

  FrameSink* m_sink;
  int m_width = 1280;
  int m_height = 720;
  bool m_recording = false;
  std::uint32_t m_next_seq = 1;
  std::uint32_t m_frames_sent = 0;
  std::uint32_t m_frames_dropped = 0;

  std::vector<Texture> m_textures;  // index = TextureId - 1
  TextureId m_font = kNoTexture;

  std::vector<std::uint8_t> m_resources;  // queued kTexture / kDestroyTexture commands
  std::vector<std::uint8_t> m_draws;      // this frame's draw commands
  std::vector<std::uint8_t> m_frame;      // header + resources + draws, handed to the sink
  // The open batch: its op, texture, and where its vertex count is.
  std::uint32_t m_batch_op = 0;
  TextureId m_batch_texture = kNoTexture;
  std::size_t m_batch_count_at = 0;
  std::uint32_t m_batch_count = 0;
};

}  // namespace scraps::webview2
