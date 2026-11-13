#include "backends/webview2/frame_format.h"

#include <cstring>

namespace scraps::webview2 {

void store_u32(std::uint8_t* at, std::uint32_t value) {
  at[0] = static_cast<std::uint8_t>(value & 0xFFu);
  at[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  at[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  at[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

namespace {

void store_f32(std::uint8_t* at, float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "f32 is 32 bits");
  std::memcpy(&bits, &value, sizeof(bits));
  store_u32(at, bits);
}

}  // namespace

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  const std::size_t at = out.size();
  out.resize(at + 4);  // within capacity after the first frames: no allocation
  store_u32(out.data() + at, value);
}

void put_f32(std::vector<std::uint8_t>& out, float value) {
  const std::size_t at = out.size();
  out.resize(at + 4);
  store_f32(out.data() + at, value);
}

void put_vertex(std::vector<std::uint8_t>& out, const Vertex2D& vertex) {
  const std::size_t at = out.size();
  out.resize(at + kVertexBytes);
  std::uint8_t* p = out.data() + at;
  store_f32(p, vertex.x);
  store_f32(p + 4, vertex.y);
  p[8] = vertex.color.r;
  p[9] = vertex.color.g;
  p[10] = vertex.color.b;
  p[11] = vertex.color.a;
  store_f32(p + 12, vertex.u);
  store_f32(p + 16, vertex.v);
}

std::uint32_t read_u32(const std::uint8_t* at) {
  return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8) |
         (static_cast<std::uint32_t>(at[2]) << 16) | (static_cast<std::uint32_t>(at[3]) << 24);
}

float read_f32(const std::uint8_t* at) {
  const std::uint32_t bits = read_u32(at);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::uint32_t pack_rgba(Color color) {
  return static_cast<std::uint32_t>(color.r) | (static_cast<std::uint32_t>(color.g) << 8) |
         (static_cast<std::uint32_t>(color.b) << 16) | (static_cast<std::uint32_t>(color.a) << 24);
}

Color unpack_rgba(std::uint32_t rgba) {
  return Color(static_cast<std::uint8_t>(rgba & 0xFFu), static_cast<std::uint8_t>((rgba >> 8) & 0xFFu),
               static_cast<std::uint8_t>((rgba >> 16) & 0xFFu), static_cast<std::uint8_t>((rgba >> 24) & 0xFFu));
}

void write_buffer_header(std::uint8_t* out, std::uint32_t slot_capacity, std::uint32_t generation) {
  std::memset(out, 0, kBufferHeaderBytes);
  store_u32(out, kBufferMagic);
  store_u32(out + 4, kFormatVersion);
  store_u32(out + 8, kSlotCount);
  store_u32(out + 12, slot_capacity);
  store_u32(out + 16, generation);
}

Vertex2D read_vertex(const std::uint8_t* at) {
  Vertex2D vertex;
  vertex.x = read_f32(at);
  vertex.y = read_f32(at + 4);
  vertex.color = Color(at[8], at[9], at[10], at[11]);
  vertex.u = read_f32(at + 12);
  vertex.v = read_f32(at + 16);
  return vertex;
}

bool decode_frame(const std::uint8_t* bytes, std::size_t size, DecodedFrame& out) {
  out = DecodedFrame();
  if (bytes == nullptr || size < kFrameHeaderBytes) return false;
  if (read_u32(bytes) != kFrameMagic || read_u32(bytes + 4) != kFormatVersion) return false;
  out.seq = read_u32(bytes + 8);
  out.total = read_u32(bytes + 12);
  out.width = read_u32(bytes + 16);
  out.height = read_u32(bytes + 20);
  out.draw_offset = read_u32(bytes + 24);
  const std::size_t total = out.total;
  const std::size_t draw_offset = out.draw_offset;
  if (total > size || total < kFrameHeaderBytes || total % 4 != 0) return false;
  if (draw_offset < kFrameHeaderBytes || draw_offset > total || draw_offset % 4 != 0) return false;

  std::size_t at = kFrameHeaderBytes;
  while (at < total) {
    // Every read below is checked against what is left: `total - at`.
    const std::size_t left = total - at;
    if (left < 4) return false;
    const std::uint32_t op = read_u32(bytes + at);
    const bool resources = at < draw_offset;
    DecodedCommand command;
    std::size_t length = 0;
    switch (op) {
      case static_cast<std::uint32_t>(FrameOp::kClear):
        if (resources || left < 8) return false;
        command.op = FrameOp::kClear;
        command.rgba = read_u32(bytes + at + 4);
        length = 8;
        break;
      case static_cast<std::uint32_t>(FrameOp::kTriangles): {
        if (resources || left < 12) return false;
        command.op = FrameOp::kTriangles;
        command.texture = read_u32(bytes + at + 4);
        command.count = read_u32(bytes + at + 8);
        if (command.count % 3 != 0 || command.count > (left - 12) / kVertexBytes) return false;
        command.offset = at + 12;
        length = 12 + command.count * kVertexBytes;
        break;
      }
      case static_cast<std::uint32_t>(FrameOp::kLines): {
        if (resources || left < 8) return false;
        command.op = FrameOp::kLines;
        command.count = read_u32(bytes + at + 4);
        if (command.count % 2 != 0 || command.count > (left - 8) / kVertexBytes) return false;
        command.offset = at + 8;
        length = 8 + command.count * kVertexBytes;
        break;
      }
      case static_cast<std::uint32_t>(FrameOp::kTexture): {
        if (!resources || left < 16) return false;
        command.op = FrameOp::kTexture;
        command.texture = read_u32(bytes + at + 4);
        command.width = read_u32(bytes + at + 8);
        command.height = read_u32(bytes + at + 12);
        if (command.texture == 0 || command.width == 0 || command.height == 0 || command.width > kMaxTextureSide ||
            command.height > kMaxTextureSide) {
          return false;
        }
        const std::size_t pixels = static_cast<std::size_t>(command.width) * command.height;
        if (pixels > (left - 16) / 4) return false;
        command.offset = at + 16;
        length = 16 + pixels * 4;
        break;
      }
      case static_cast<std::uint32_t>(FrameOp::kDestroyTexture):
        if (!resources || left < 8) return false;
        command.op = FrameOp::kDestroyTexture;
        command.texture = read_u32(bytes + at + 4);
        length = 8;
        break;
      default:
        return false;
    }
    // A resource may not run over into the draws, nor a draw past the end.
    if (resources && at + length > draw_offset) return false;
    out.commands.push_back(command);
    at += length;
  }
  return true;
}

}  // namespace scraps::webview2
