#include "core/serialize.h"

#include <array>
#include <cmath>
#include <cstring>

#include "core/world.h"

namespace scraps {
namespace {

constexpr std::uint32_t kWorldMagic = 0x57524353u;  // "SCRW" in little-endian bytes
constexpr std::uint16_t kWorldFormatVersion = 3;  // 3: sky, sounds, entity facing

// Limits for values read from untrusted bytes. Looser than what the engine
// produces (kWorldExtent, scene limits), so valid worlds always pass.
constexpr float kMaxCoordinateValue = 1.0e6f;
constexpr float kMaxSizeValue = 64.0f;
constexpr float kMaxSpeedValue = 1000.0f;

bool coordinates_in_range(const Vec3& v) {
  return std::fabs(v.x) <= kMaxCoordinateValue && std::fabs(v.y) <= kMaxCoordinateValue &&
         std::fabs(v.z) <= kMaxCoordinateValue;
}

bool size_in_range(const Vec3& v) {
  return v.x >= 0.0f && v.y >= 0.0f && v.z >= 0.0f && v.x <= kMaxSizeValue && v.y <= kMaxSizeValue &&
         v.z <= kMaxSizeValue;
}

void write_entity(ByteWriter& w, const Entity& e) {
  w.write_u16(e.archetype);
  w.write_u16(e.kind);
  w.write_u32(e.flags);
  w.write_vec3(e.position);
  w.write_vec3(e.velocity);
  w.write_vec3(e.size);
  w.write_u8(e.player);
  w.write_vec2(e.move_intent);
  w.write_vec2(e.facing);
  w.write_f32(e.move_speed);
  w.write_bool(e.on_ground);
  w.write_color(e.color);
  for (std::int32_t value : e.user) w.write_i32(value);
}

// Checks the values of a live entity read from untrusted bytes.
bool entity_values_ok(const Entity& e, std::size_t archetype_count) {
  if (e.archetype >= archetype_count) return false;
  if (e.player != kNoPlayer && e.player >= kMaxPlayers) return false;
  // Bounds keep hostile values from hanging collision loops (huge boxes) or
  // overflowing float-to-int conversions (huge positions) later on.
  if (!size_in_range(e.size) || !coordinates_in_range(e.position) || !coordinates_in_range(e.velocity)) return false;
  if (std::fabs(e.move_intent.x) > 1.01f || std::fabs(e.move_intent.y) > 1.01f) return false;
  if (std::fabs(e.facing.x) > 1.01f || std::fabs(e.facing.y) > 1.01f) return false;
  if (e.move_speed < 0.0f || e.move_speed > kMaxSpeedValue) return false;
  return true;
}

bool read_entity(ByteReader& r, Entity& e, std::size_t archetype_count) {
  e.archetype = r.read_u16();
  e.kind = r.read_u16();
  e.flags = r.read_u32();
  e.position = r.read_vec3();
  e.velocity = r.read_vec3();
  e.size = r.read_vec3();
  e.player = r.read_u8();
  e.move_intent = r.read_vec2();
  e.facing = r.read_vec2();
  e.move_speed = r.read_f32();
  e.on_ground = r.read_bool();
  e.color = r.read_color();
  for (std::int32_t& value : e.user) value = r.read_i32();
  return r.ok() && entity_values_ok(e, archetype_count);
}

// A dead slot carries nothing but its generation; everything else is reset,
// so a rebuilt state is the same whichever way it was rebuilt.
Entity empty_slot(std::uint32_t generation) {
  Entity entity;
  entity.generation = generation;
  return entity;
}

// The free list must be exactly the dead slots, each once: a duplicate would
// let two spawns share one slot. Needs dead slots to be reset (empty_slot):
// their `flags` field is used as a "seen" mark while checking.
bool free_list_is_valid(World& world) {
  std::size_t dead_count = 0;
  for (const Entity& entity : world.entities) {
    if (entity.alive) continue;
    if (entity.flags != 0) return false;
    ++dead_count;
  }
  if (world.free_slots.size() != dead_count) return false;
  const std::size_t slot_count = world.entities.size();
  bool unique = true;
  for (std::uint32_t slot : world.free_slots) {
    if (slot >= slot_count || world.entities[slot].alive) {
      unique = false;
      break;
    }
    Entity& dead = world.entities[slot];
    if (dead.flags != 0) unique = false;
    dead.flags = 1;
  }
  for (std::uint32_t slot : world.free_slots) {
    if (slot < slot_count && !world.entities[slot].alive) world.entities[slot].flags = 0;
  }
  return unique;
}

bool player_table_is_valid(const World& world) {
  for (const EntityId& id : world.player_entities) {
    if (id.index != EntityId::kInvalidIndex && id.index >= world.entities.size()) return false;
  }
  return true;
}

// ---------------------------------------------------------------- delta groups
//
// Each changed entity in a delta carries a mask of the groups that follow.
// Fields are grouped by how often they change (spec: "What changes means").
constexpr std::uint8_t kGroupSlot = 1u << 0;     // alive, generation
constexpr std::uint8_t kGroupPose = 1u << 1;     // position
constexpr std::uint8_t kGroupMotion = 1u << 2;   // velocity, on_ground
constexpr std::uint8_t kGroupControl = 1u << 3;  // player, move_intent, facing
constexpr std::uint8_t kGroupLook = 1u << 4;     // archetype, kind, flags, size, color, move_speed
constexpr std::uint8_t kGroupGame = 1u << 5;     // user[4]
constexpr std::uint8_t kAllGroups = 0x3Fu;

// Compares floats by their bits, so 0.0 versus -0.0 counts as a change and
// the rebuilt state is byte-for-byte the sender's.
bool same_bits(float a, float b) {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::memcpy(&x, &a, sizeof(x));
  std::memcpy(&y, &b, sizeof(y));
  return x == y;
}
bool same_bits(const Vec2& a, const Vec2& b) { return same_bits(a.x, b.x) && same_bits(a.y, b.y); }
bool same_bits(const Vec3& a, const Vec3& b) {
  return same_bits(a.x, b.x) && same_bits(a.y, b.y) && same_bits(a.z, b.z);
}
bool same_color(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

std::uint8_t changed_groups(const Entity& before, const Entity& after) {
  if (!after.alive) {  // a dead slot has nothing but its generation
    return before.alive || before.generation != after.generation ? kGroupSlot : 0;
  }
  // Born (or reborn) since the baseline: the old fields mean nothing.
  if (!before.alive || before.generation != after.generation) return kAllGroups;
  std::uint8_t mask = 0;
  if (!same_bits(before.position, after.position)) mask |= kGroupPose;
  if (!same_bits(before.velocity, after.velocity) || before.on_ground != after.on_ground) mask |= kGroupMotion;
  if (before.player != after.player || !same_bits(before.move_intent, after.move_intent) ||
      !same_bits(before.facing, after.facing)) {
    mask |= kGroupControl;
  }
  if (before.archetype != after.archetype || before.kind != after.kind || before.flags != after.flags ||
      !same_bits(before.size, after.size) || !same_color(before.color, after.color) ||
      !same_bits(before.move_speed, after.move_speed)) {
    mask |= kGroupLook;
  }
  if (before.user != after.user) mask |= kGroupGame;
  return mask;
}

void write_groups(ByteWriter& w, const Entity& e, std::uint8_t mask) {
  if ((mask & kGroupSlot) != 0) {
    w.write_u32(e.generation);
    w.write_bool(e.alive);
  }
  if ((mask & kGroupPose) != 0) w.write_vec3(e.position);
  if ((mask & kGroupMotion) != 0) {
    w.write_vec3(e.velocity);
    w.write_bool(e.on_ground);
  }
  if ((mask & kGroupControl) != 0) {
    w.write_u8(e.player);
    w.write_vec2(e.move_intent);
    w.write_vec2(e.facing);
  }
  if ((mask & kGroupLook) != 0) {
    w.write_u16(e.archetype);
    w.write_u16(e.kind);
    w.write_u32(e.flags);
    w.write_vec3(e.size);
    w.write_color(e.color);
    w.write_f32(e.move_speed);
  }
  if ((mask & kGroupGame) != 0) {
    for (std::int32_t value : e.user) w.write_i32(value);
  }
}

// Reads the groups after the slot group. The caller validates the result.
void read_live_groups(ByteReader& r, Entity& e, std::uint8_t mask) {
  if ((mask & kGroupPose) != 0) e.position = r.read_vec3();
  if ((mask & kGroupMotion) != 0) {
    e.velocity = r.read_vec3();
    e.on_ground = r.read_bool();
  }
  if ((mask & kGroupControl) != 0) {
    e.player = r.read_u8();
    e.move_intent = r.read_vec2();
    e.facing = r.read_vec2();
  }
  if ((mask & kGroupLook) != 0) {
    e.archetype = r.read_u16();
    e.kind = r.read_u16();
    e.flags = r.read_u32();
    e.size = r.read_vec3();
    e.color = r.read_color();
    e.move_speed = r.read_f32();
  }
  if ((mask & kGroupGame) != 0) {
    for (std::int32_t& value : e.user) value = r.read_i32();
  }
}

void write_player_table(ByteWriter& w, const std::array<EntityId, kMaxPlayers>& table) {
  for (const EntityId& id : table) {
    w.write_u32(id.index);
    w.write_u32(id.generation);
  }
}

void read_player_table(ByteReader& r, std::array<EntityId, kMaxPlayers>& table) {
  for (EntityId& id : table) {
    const std::uint32_t index = r.read_u32();
    const std::uint32_t generation = r.read_u32();
    id = EntityId(index, generation);
  }
}

}  // namespace

// ---------------------------------------------------------------- ByteWriter

void ByteWriter::write_u16(std::uint16_t value) {
  m_out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  m_out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void ByteWriter::write_u32(std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) m_out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
}

void ByteWriter::write_u64(std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) m_out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
}

void ByteWriter::write_f32(float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float must be 32 bits");
  std::memcpy(&bits, &value, sizeof(bits));
  write_u32(bits);
}

void ByteWriter::write_varint(std::uint32_t value) {
  while (value >= 0x80u) {
    m_out.push_back(static_cast<std::uint8_t>((value & 0x7Fu) | 0x80u));
    value >>= 7;
  }
  m_out.push_back(static_cast<std::uint8_t>(value));
}

void ByteWriter::write_bytes(const std::uint8_t* data, std::size_t size) {
  if (size == 0) return;
  m_out.insert(m_out.end(), data, data + size);
}

void ByteWriter::write_vec2(const Vec2& v) {
  write_f32(v.x);
  write_f32(v.y);
}

void ByteWriter::write_vec3(const Vec3& v) {
  write_f32(v.x);
  write_f32(v.y);
  write_f32(v.z);
}

void ByteWriter::write_color(Color color) {
  write_u8(color.r);
  write_u8(color.g);
  write_u8(color.b);
  write_u8(color.a);
}

void ByteWriter::write_name(const Name& name) {
  for (char c : name) write_u8(static_cast<std::uint8_t>(c));
}

// ---------------------------------------------------------------- ByteReader

bool ByteReader::take(std::size_t size) {
  if (!m_ok || m_size - m_position < size) {
    m_ok = false;
    return false;
  }
  return true;
}

std::uint8_t ByteReader::read_u8() {
  if (!take(1)) return 0;
  return m_data[m_position++];
}

std::uint16_t ByteReader::read_u16() {
  if (!take(2)) return 0;
  const std::uint16_t value =
      static_cast<std::uint16_t>(m_data[m_position] | (static_cast<std::uint16_t>(m_data[m_position + 1]) << 8));
  m_position += 2;
  return value;
}

std::uint32_t ByteReader::read_u32() {
  if (!take(4)) return 0;
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(m_data[m_position + static_cast<std::size_t>(i)]) << (8 * i);
  m_position += 4;
  return value;
}

std::uint64_t ByteReader::read_u64() {
  if (!take(8)) return 0;
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(m_data[m_position + static_cast<std::size_t>(i)]) << (8 * i);
  m_position += 8;
  return value;
}

float ByteReader::read_f32() {
  const std::uint32_t bits = read_u32();
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  if (!std::isfinite(value)) {
    m_ok = false;
    return 0.0f;
  }
  return value;
}

bool ByteReader::read_bool() {
  const std::uint8_t value = read_u8();
  if (value > 1) m_ok = false;
  return value == 1;
}

std::uint32_t ByteReader::read_varint() {
  std::uint32_t value = 0;
  for (int shift = 0; shift < 35; shift += 7) {
    const std::uint8_t byte = read_u8();
    if (!m_ok) return 0;
    // The fifth byte may only hold the top 4 bits of a 32-bit value.
    if (shift == 28 && byte > 0x0Fu) break;
    value |= static_cast<std::uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0) return value;
  }
  m_ok = false;
  return 0;
}

bool ByteReader::read_bytes(std::uint8_t* out, std::size_t size) {
  if (!take(size)) return false;
  if (size > 0) std::memcpy(out, m_data + m_position, size);
  m_position += size;
  return true;
}

const std::uint8_t* ByteReader::skip(std::size_t size) {
  if (!take(size)) return nullptr;
  const std::uint8_t* start = m_data + m_position;
  m_position += size;
  return start;
}

Vec2 ByteReader::read_vec2() {
  const float x = read_f32();
  const float y = read_f32();
  return Vec2(x, y);
}

Vec3 ByteReader::read_vec3() {
  const float x = read_f32();
  const float y = read_f32();
  const float z = read_f32();
  return Vec3(x, y, z);
}

Color ByteReader::read_color() {
  const std::uint8_t r = read_u8();
  const std::uint8_t g = read_u8();
  const std::uint8_t b = read_u8();
  const std::uint8_t a = read_u8();
  return Color(r, g, b, a);
}

Name ByteReader::read_name() {
  Name name{};
  for (char& c : name) c = static_cast<char>(read_u8());
  name[kNameCapacity - 1] = '\0';
  return name;
}

// ---------------------------------------------------------------- rules

void write_rules(ByteWriter& writer, const MovementRules& rules) {
  writer.write_vec3(rules.gravity);
  writer.write_f32(rules.jump_speed);
  writer.write_bool(rules.move_x);
  writer.write_bool(rules.move_y);
  writer.write_f32(rules.kill_z);
}

bool read_rules(ByteReader& reader, MovementRules& rules) {
  rules.gravity = reader.read_vec3();
  rules.jump_speed = reader.read_f32();
  rules.move_x = reader.read_bool();
  rules.move_y = reader.read_bool();
  rules.kill_z = reader.read_f32();
  return reader.ok() && coordinates_in_range(rules.gravity) && std::fabs(rules.jump_speed) <= kMaxSpeedValue &&
         std::fabs(rules.kill_z) <= kMaxCoordinateValue;
}

// ---------------------------------------------------------------- level

void write_level(ByteWriter& writer, const Level& level) {
  writer.write_u8(static_cast<std::uint8_t>(level.perspective));

  writer.write_u32(static_cast<std::uint32_t>(level.images.size()));
  for (const ImageDef& image : level.images) {
    writer.write_name(image.name);
    for (char c : image.path) writer.write_u8(static_cast<std::uint8_t>(c));
    writer.write_u16(image.frames);
    writer.write_u16(image.frame_width);
    writer.write_u16(image.frame_height);
    writer.write_f32(image.fps);
  }

  writer.write_u32(static_cast<std::uint32_t>(level.tiles.size()));
  for (const TileDef& tile : level.tiles) {
    writer.write_name(tile.name);
    writer.write_u8(static_cast<std::uint8_t>(tile.symbol));
    writer.write_bool(tile.solid);
    writer.write_color(tile.color);
    writer.write_u16(tile.image);
  }

  writer.write_u32(static_cast<std::uint32_t>(level.grid.width));
  writer.write_u32(static_cast<std::uint32_t>(level.grid.depth));
  writer.write_u32(static_cast<std::uint32_t>(level.grid.height));
  writer.write_bytes(level.grid.cells.data(), level.grid.cells.size());

  writer.write_u32(static_cast<std::uint32_t>(level.archetypes.size()));
  for (const Archetype& archetype : level.archetypes) {
    writer.write_name(archetype.name);
    writer.write_u16(archetype.kind);
    writer.write_u32(archetype.flags);
    writer.write_vec3(archetype.size);
    writer.write_color(archetype.color);
    writer.write_f32(archetype.move_speed);
    writer.write_u16(archetype.image);
  }
  writer.write_u16(level.player_archetype);

  writer.write_u32(static_cast<std::uint32_t>(level.spawn_points.size()));
  for (const Vec3& spawn : level.spawn_points) writer.write_vec3(spawn);

  writer.write_u32(static_cast<std::uint32_t>(level.sounds.size()));
  for (const SoundDef& sound : level.sounds) {
    writer.write_name(sound.name);
    for (char c : sound.path) writer.write_u8(static_cast<std::uint8_t>(c));
    writer.write_f32(sound.volume);
  }
  writer.write_color(level.sky_top);
  writer.write_color(level.sky_bottom);
}

bool read_level(ByteReader& reader, Level& level) {
  const std::uint8_t perspective = reader.read_u8();
  if (perspective > static_cast<std::uint8_t>(Perspective::kIsometric)) return false;
  level.perspective = static_cast<Perspective>(perspective);

  const std::uint32_t image_count = reader.read_u32();
  if (!reader.ok() || image_count > kMaxImages) return false;
  level.images.resize(image_count);
  for (ImageDef& image : level.images) {
    image.name = reader.read_name();
    for (char& c : image.path) c = static_cast<char>(reader.read_u8());
    image.path[kPathCapacity - 1] = '\0';
    image.frames = reader.read_u16();
    image.frame_width = reader.read_u16();
    image.frame_height = reader.read_u16();
    image.fps = reader.read_f32();
    if (!reader.ok() || !is_valid_asset_path(path_view(image.path))) return false;
    if (image.frames == 0 || image.frames > kMaxImageFrames || image.fps < 0.0f || image.fps > 120.0f) return false;
  }
  auto image_ok = [image_count](ImageIndex index) {
    return index == kNoImage || static_cast<std::uint32_t>(index) < image_count;
  };

  const std::uint32_t tile_count = reader.read_u32();
  if (!reader.ok() || tile_count == 0 || tile_count > kMaxTileDefs) return false;
  level.tiles.resize(tile_count);
  for (TileDef& tile : level.tiles) {
    tile.name = reader.read_name();
    tile.symbol = static_cast<char>(reader.read_u8());
    tile.solid = reader.read_bool();
    tile.color = reader.read_color();
    tile.image = reader.read_u16();
    if (!image_ok(tile.image)) return false;
  }

  const std::uint32_t width = reader.read_u32();
  const std::uint32_t depth = reader.read_u32();
  const std::uint32_t height = reader.read_u32();
  if (!reader.ok() || width > static_cast<std::uint32_t>(kMaxGridSide) ||
      depth > static_cast<std::uint32_t>(kMaxGridSide) || height > static_cast<std::uint32_t>(kMaxGridSide)) {
    return false;
  }
  const std::size_t cell_count = static_cast<std::size_t>(width) * depth * height;
  if (cell_count > kMaxGridCells || reader.remaining() < cell_count) return false;
  level.grid.resize(static_cast<int>(width), static_cast<int>(depth), static_cast<int>(height));
  if (!reader.read_bytes(level.grid.cells.data(), cell_count)) return false;
  for (TileId id : level.grid.cells) {
    if (id >= tile_count) return false;
  }

  const std::uint32_t archetype_count = reader.read_u32();
  if (!reader.ok() || archetype_count > kMaxArchetypes) return false;
  level.archetypes.resize(archetype_count);
  for (Archetype& archetype : level.archetypes) {
    archetype.name = reader.read_name();
    archetype.kind = reader.read_u16();
    archetype.flags = reader.read_u32();
    archetype.size = reader.read_vec3();
    archetype.color = reader.read_color();
    archetype.move_speed = reader.read_f32();
    archetype.image = reader.read_u16();
    if (!size_in_range(archetype.size) || archetype.move_speed < 0.0f || archetype.move_speed > kMaxSpeedValue ||
        !image_ok(archetype.image)) {
      return false;
    }
  }
  level.player_archetype = reader.read_u16();
  if (!reader.ok() || (archetype_count > 0 && level.player_archetype >= archetype_count)) return false;

  const std::uint32_t spawn_count = reader.read_u32();
  if (!reader.ok() || spawn_count > kMaxSpawnPoints) return false;
  level.spawn_points.resize(spawn_count);
  for (Vec3& spawn : level.spawn_points) {
    spawn = reader.read_vec3();
    if (!coordinates_in_range(spawn)) return false;
  }

  const std::uint32_t sound_count = reader.read_u32();
  if (!reader.ok() || sound_count > kMaxSounds) return false;
  level.sounds.resize(sound_count);
  for (SoundDef& sound : level.sounds) {
    sound.name = reader.read_name();
    for (char& c : sound.path) c = static_cast<char>(reader.read_u8());
    sound.path[kPathCapacity - 1] = '\0';
    sound.volume = reader.read_f32();
    if (!reader.ok() || !is_valid_asset_path(path_view(sound.path))) return false;
    if (sound.volume < 0.0f || sound.volume > 4.0f) return false;
  }
  level.sky_top = reader.read_color();
  level.sky_bottom = reader.read_color();
  return reader.ok();
}

// ---------------------------------------------------------------- world state

void write_world_state(ByteWriter& writer, const World& world) {
  writer.write_u32(world.tick);
  writer.write_u64(world.rng_state);

  writer.write_u32(static_cast<std::uint32_t>(world.entities.size()));
  for (const Entity& entity : world.entities) {
    writer.write_u32(entity.generation);
    writer.write_bool(entity.alive);
    if (entity.alive) write_entity(writer, entity);
  }

  writer.write_u32(static_cast<std::uint32_t>(world.free_slots.size()));
  for (std::uint32_t slot : world.free_slots) writer.write_u32(slot);

  write_player_table(writer, world.player_entities);
}

bool read_world_state(ByteReader& reader, World& world) {
  world.tick = reader.read_u32();
  world.rng_state = reader.read_u64();

  const std::uint32_t slot_count = reader.read_u32();
  // Each slot is at least 5 bytes, which also bounds the allocation below by
  // the size of the input.
  if (!reader.ok() || slot_count > kMaxEntities || reader.remaining() / 5 < slot_count) return false;
  world.entities.resize(slot_count);
  const std::size_t archetype_count = world.level.archetypes.size();
  for (Entity& entity : world.entities) {
    entity = empty_slot(reader.read_u32());
    entity.alive = reader.read_bool();
    if (!reader.ok()) return false;
    if (entity.alive && !read_entity(reader, entity, archetype_count)) return false;
  }

  const std::uint32_t free_count = reader.read_u32();
  if (!reader.ok() || free_count > slot_count || reader.remaining() / 4 < free_count) return false;
  world.free_slots.resize(free_count);
  for (std::uint32_t& slot : world.free_slots) slot = reader.read_u32();
  if (!reader.ok() || !free_list_is_valid(world)) return false;

  read_player_table(reader, world.player_entities);
  return reader.ok() && player_table_is_valid(world);
}

// ---------------------------------------------------------------- delta
//
//   baseline tick u32 | ticks since the baseline varint | rng u64 | slot count varint
//   | changed count varint, then per changed entity:
//       (unchanged slots skipped since the previous one, varint) (group mask u8) (groups...)
//   | free list:    0 = same as the baseline, or 1, count varint, slots u32...
//   | player table: 0 = same as the baseline, or 1, kMaxPlayers x (index u32, generation u32)

void write_world_delta(ByteWriter& writer, const WorldState& baseline, const World& current) {
  // The baseline's tick lets the reader refuse a delta applied to the wrong
  // baseline (a mix-up would otherwise rebuild a plausible but wrong world).
  writer.write_u32(baseline.tick);
  writer.write_varint(current.tick - baseline.tick);
  writer.write_u64(current.rng_state);

  const std::size_t slot_count = current.entities.size();
  const std::size_t base_count = baseline.entities.size();
  const Entity never_used;  // what a slot the baseline does not have yet looks like
  auto before = [&](std::size_t i) -> const Entity& { return i < base_count ? baseline.entities[i] : never_used; };

  std::uint32_t changed = 0;
  for (std::size_t i = 0; i < slot_count; ++i) {
    if (changed_groups(before(i), current.entities[i]) != 0) ++changed;
  }
  writer.write_varint(static_cast<std::uint32_t>(slot_count));
  writer.write_varint(changed);
  std::size_t next = 0;
  for (std::size_t i = 0; i < slot_count; ++i) {
    const Entity& entity = current.entities[i];
    const std::uint8_t mask = changed_groups(before(i), entity);
    if (mask == 0) continue;
    writer.write_varint(static_cast<std::uint32_t>(i - next));
    writer.write_u8(mask);
    write_groups(writer, entity, mask);
    next = i + 1;
  }

  if (current.free_slots == baseline.free_slots) {
    writer.write_u8(0);
  } else {
    writer.write_u8(1);
    writer.write_varint(static_cast<std::uint32_t>(current.free_slots.size()));
    for (std::uint32_t slot : current.free_slots) writer.write_u32(slot);
  }

  if (current.player_entities == baseline.player_entities) {
    writer.write_u8(0);
  } else {
    writer.write_u8(1);
    write_player_table(writer, current.player_entities);
  }
}

bool read_world_delta(ByteReader& reader, const WorldState& baseline, World& out) {
  if (reader.read_u32() != baseline.tick || !reader.ok()) return false;
  out.tick = baseline.tick + reader.read_varint();
  out.rng_state = reader.read_u64();

  const std::uint32_t slot_count = reader.read_varint();
  if (!reader.ok() || slot_count > kMaxEntities) return false;
  const std::size_t base_count = baseline.entities.size();
  // A slot the baseline does not have is either written in full in the delta
  // (alive: every group) or dead and listed in an explicit free list (4
  // bytes): at least 4 bytes each, which bounds the allocation below by the
  // size of the input.
  if (slot_count > base_count && slot_count - base_count > reader.remaining() / 4) return false;

  out.entities.resize(slot_count);
  for (std::size_t i = 0; i < slot_count; ++i) {
    if (i >= base_count) {
      out.entities[i] = Entity();
    } else if (baseline.entities[i].alive) {
      out.entities[i] = baseline.entities[i];
    } else {
      out.entities[i] = empty_slot(baseline.entities[i].generation);
    }
  }

  const std::uint32_t changed = reader.read_varint();
  if (!reader.ok() || changed > slot_count) return false;
  const std::size_t archetype_count = out.level.archetypes.size();
  std::uint64_t next = 0;
  for (std::uint32_t c = 0; c < changed; ++c) {
    const std::uint64_t index = next + reader.read_varint();
    const std::uint8_t mask = reader.read_u8();
    if (!reader.ok() || index >= slot_count || mask == 0 || (mask & ~kAllGroups) != 0) return false;
    next = index + 1;

    Entity& entity = out.entities[static_cast<std::size_t>(index)];
    if ((mask & kGroupSlot) != 0) {
      const bool was_alive = entity.alive;
      const std::uint32_t old_generation = entity.generation;
      const std::uint32_t generation = reader.read_u32();
      const bool alive = reader.read_bool();
      if (!reader.ok()) return false;
      if (!alive) {
        if (mask != kGroupSlot) return false;  // a dead slot has no other fields
        entity = empty_slot(generation);
        continue;
      }
      // A newborn entity must come with every field: nothing of the old slot is kept.
      if ((!was_alive || old_generation != generation) && mask != kAllGroups) return false;
      entity.generation = generation;
      entity.alive = true;
    } else if (!entity.alive) {
      return false;  // only a live entity has fields to change
    }
    read_live_groups(reader, entity, mask);
    if (!reader.ok() || !entity_values_ok(entity, archetype_count)) return false;
  }

  const std::uint8_t free_flag = reader.read_u8();
  if (!reader.ok() || free_flag > 1) return false;
  if (free_flag == 0) {
    out.free_slots = baseline.free_slots;
  } else {
    const std::uint32_t free_count = reader.read_varint();
    if (!reader.ok() || free_count > slot_count || reader.remaining() / 4 < free_count) return false;
    out.free_slots.resize(free_count);
    for (std::uint32_t& slot : out.free_slots) slot = reader.read_u32();
    if (!reader.ok()) return false;
  }
  if (!free_list_is_valid(out)) return false;

  const std::uint8_t player_flag = reader.read_u8();
  if (!reader.ok() || player_flag > 1) return false;
  if (player_flag == 0) {
    out.player_entities = baseline.player_entities;
  } else {
    read_player_table(reader, out.player_entities);
  }
  return reader.ok() && player_table_is_valid(out);
}

// ---------------------------------------------------------------- whole world

void write_world(ByteWriter& writer, const World& world) {
  writer.write_u32(kWorldMagic);
  writer.write_u16(kWorldFormatVersion);
  write_rules(writer, world.rules);
  write_level(writer, world.level);
  write_world_state(writer, world);
}

bool read_world(ByteReader& reader, World& world) {
  if (reader.read_u32() != kWorldMagic) return false;
  if (reader.read_u16() != kWorldFormatVersion) return false;
  if (!read_rules(reader, world.rules)) return false;
  if (!read_level(reader, world.level)) return false;
  return read_world_state(reader, world);
}

}  // namespace scraps
