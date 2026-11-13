#pragma once

// The bytes one drawn frame becomes on its way to the page (docs/specs/
// 2026-11-14-offline-essentials-and-ui.md, part 6, layout A). Renderer2D
// calls are recorded natively (batch_renderer.h), copied into a slot of a
// WebView2 shared buffer, and drawn by WebGL2 in renderer.js, which decodes
// exactly this format. No JSON per vertex: per frame only a ~20-byte "frame
// ready" message crosses as text.
//
// Everything is little-endian and 4-byte aligned, so the page reads floats
// with a Float32Array/DataView and uploads vertices straight from the buffer.
//
//   shared buffer:  [buffer header, 64 bytes][slot 0][slot 1][slot 2]
//     buffer header: u32 magic "SCRB", u32 version, u32 slot count,
//                    u32 slot capacity in bytes, u32 generation, rest 0
//   one frame, at the start of a slot:
//     frame header (32 bytes): u32 magic "SCRF", u32 version, u32 seq,
//                    u32 total bytes (header included), u32 width,
//                    u32 height, u32 draw offset, u32 0
//     resources, from byte 32 to the draw offset (processed even when the
//     page skips drawing the frame):
//       kTexture         u32 op, u32 id, u32 width, u32 height, w*h*4 RGBA bytes
//       kDestroyTexture  u32 op, u32 id
//     draws, from the draw offset to the end, in painter's order:
//       kClear           u32 op, u32 rgba (r | g<<8 | b<<16 | a<<24)
//       kTriangles       u32 op, u32 texture (0 = none), u32 vertex count, vertices
//       kLines           u32 op, u32 vertex count (pairs), vertices
//     vertex (20 bytes): f32 x, f32 y, u8 r, g, b, a, f32 u, f32 v
//
// Both decoders (decode_frame here, walkFrame in renderer.js) treat the
// bytes as untrusted: every count is bounded by what is left of the frame.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platform/color.h"
#include "platform/renderer2d.h"

namespace scraps::webview2 {

constexpr std::uint32_t kBufferMagic = 0x42524353u;  // "SCRB" in memory
constexpr std::uint32_t kFrameMagic = 0x46524353u;   // "SCRF" in memory
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint32_t kSlotCount = 3;
constexpr std::size_t kBufferHeaderBytes = 64;
constexpr std::size_t kFrameHeaderBytes = 32;
constexpr std::size_t kVertexBytes = 20;
// The largest texture side the page accepts (WebGL2 guarantees at least 2048;
// real GPUs give 16384). Bigger textures are refused natively.
constexpr std::uint32_t kMaxTextureSide = 16384;
// A slot never grows past this; a frame that would not fit is dropped.
constexpr std::size_t kMaxSlotBytes = std::size_t{64} * 1024 * 1024;

enum class FrameOp : std::uint32_t {
  kClear = 1,
  kTriangles = 2,
  kLines = 3,
  kTexture = 4,
  kDestroyTexture = 5,
};

// Appends little-endian values to a byte vector (reused frame to frame, so
// after the first frames it no longer allocates).
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value);
void put_f32(std::vector<std::uint8_t>& out, float value);
void put_vertex(std::vector<std::uint8_t>& out, const Vertex2D& vertex);
// Writes `value` at `at` (4 bytes, little-endian), for patching counts.
void store_u32(std::uint8_t* at, std::uint32_t value);
std::uint32_t read_u32(const std::uint8_t* at);
float read_f32(const std::uint8_t* at);
std::uint32_t pack_rgba(Color color);
Color unpack_rgba(std::uint32_t rgba);

// Writes the 64-byte buffer header at `out`.
void write_buffer_header(std::uint8_t* out, std::uint32_t slot_capacity, std::uint32_t generation);

// What a decoder sees, for tests and for checking a frame before it is sent.
struct DecodedCommand {
  FrameOp op = FrameOp::kClear;
  std::uint32_t texture = 0;  // kTriangles, kTexture, kDestroyTexture
  std::uint32_t width = 0;    // kTexture
  std::uint32_t height = 0;   // kTexture
  std::uint32_t rgba = 0;     // kClear
  std::size_t count = 0;      // vertices (kTriangles, kLines)
  std::size_t offset = 0;     // where the vertices or pixels start in the frame
};

struct DecodedFrame {
  std::uint32_t seq = 0;
  std::uint32_t total = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t draw_offset = 0;
  std::vector<DecodedCommand> commands;  // tests only: this allocates
};

// Checks and decodes the frame in `bytes` (`size` bytes are readable).
// False for anything malformed: a wrong magic or version, a total past
// `size`, an unaligned or misplaced offset, an unknown op, a count that runs
// past the end, a texture of size 0 or above kMaxTextureSide, a resource
// after the draw offset or a draw before it.
bool decode_frame(const std::uint8_t* bytes, std::size_t size, DecodedFrame& out);
// The vertex at `offset` (from DecodedCommand::offset plus i * kVertexBytes).
Vertex2D read_vertex(const std::uint8_t* at);

}  // namespace scraps::webview2
