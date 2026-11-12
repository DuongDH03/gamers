#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/level.h"
#include "core/math.h"
#include "core/movement.h"
#include "platform/color.h"

namespace scraps {

struct World;
struct WorldState;

// Appends values to a byte vector in a fixed little-endian layout, so the
// bytes are the same on every machine and compiler.
class ByteWriter {
 public:
  explicit ByteWriter(std::vector<std::uint8_t>& out) : m_out(out) {}

  void write_u8(std::uint8_t value) { m_out.push_back(value); }
  void write_u16(std::uint16_t value);
  void write_u32(std::uint32_t value);
  void write_u64(std::uint64_t value);
  void write_i32(std::int32_t value) { write_u32(static_cast<std::uint32_t>(value)); }
  void write_f32(float value);
  void write_bool(bool value) { write_u8(value ? 1 : 0); }
  // 1 to 5 bytes: 7 bits per byte, low bits first, top bit = "more follows".
  void write_varint(std::uint32_t value);
  void write_bytes(const std::uint8_t* data, std::size_t size);
  void write_vec2(const Vec2& v);
  void write_vec3(const Vec3& v);
  void write_color(Color color);
  void write_name(const Name& name);

  std::size_t size() const { return m_out.size(); }

 private:
  std::vector<std::uint8_t>& m_out;
};

// Reads what ByteWriter wrote. Never reads past the end: on any error it
// returns zeros and ok() becomes false, so callers check once at the end.
// Treat every byte as untrusted (it may come from the network).
class ByteReader {
 public:
  ByteReader(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size) {}

  std::uint8_t read_u8();
  std::uint16_t read_u16();
  std::uint32_t read_u32();
  std::uint64_t read_u64();
  std::int32_t read_i32() { return static_cast<std::int32_t>(read_u32()); }
  float read_f32();          // fails on NaN or infinity
  bool read_bool();          // fails on values other than 0 and 1
  std::uint32_t read_varint();  // fails on more than 5 bytes or a value over 32 bits
  bool read_bytes(std::uint8_t* out, std::size_t size);
  Vec2 read_vec2();
  Vec3 read_vec3();
  Color read_color();
  Name read_name();          // always null-terminated

  // Skips `size` bytes and returns a pointer to them, or nullptr on overrun.
  const std::uint8_t* skip(std::size_t size);

  void fail() { m_ok = false; }
  bool ok() const { return m_ok; }
  std::size_t remaining() const { return m_ok ? m_size - m_position : 0; }

 private:
  bool take(std::size_t size);

  const std::uint8_t* m_data;
  std::size_t m_size;
  std::size_t m_position = 0;
  bool m_ok = true;
};

void write_rules(ByteWriter& writer, const MovementRules& rules);
bool read_rules(ByteReader& reader, MovementRules& rules);

void write_level(ByteWriter& writer, const Level& level);
bool read_level(ByteReader& reader, Level& level);

// The parts of a World that change while it runs: tick, random state and
// entities. This is what a network snapshot carries; the Level is static and
// both sides load it from the same scene file.
void write_world_state(ByteWriter& writer, const World& world);
// Overwrites the state of `world`, keeping its rules and level. Validates
// everything; returns false on malformed input (then `world` is unusable
// until the next successful read).
bool read_world_state(ByteReader& reader, World& world);

// A snapshot as the changes from an older state (`baseline`) to `current`:
// only entities that differ are written, and of those only the groups of
// fields that differ (docs/specs/2026-11-12-delta-snapshots.md). Lossless:
// reading it back over the same baseline gives exactly `current`'s state.
void write_world_delta(ByteWriter& writer, const WorldState& baseline, const World& current);
// Rebuilds a state from `baseline` plus a delta into `out`, keeping its rules
// and level. Validates everything, like read_world_state; returns false on
// malformed input, including a delta written against a different baseline
// (then `out` is unusable until the next successful read).
bool read_world_delta(ByteReader& reader, const WorldState& baseline, World& out);

// The whole World: rules, level and state. For saves, replays and tests.
void write_world(ByteWriter& writer, const World& world);
bool read_world(ByteReader& reader, World& world);

}  // namespace scraps
