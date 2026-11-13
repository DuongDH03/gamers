#include "core/save.h"

#include <utility>

#include "core/scene.h"
#include "core/serialize.h"

namespace scraps {
namespace {

constexpr std::size_t kChecksumSize = 4;

// FNV-1a over raw bytes. Each step is a bijection of the running hash, so
// changing any single byte always changes the result: every one-byte
// corruption is caught. (Not a defence against someone forging a save on
// purpose; the state is validated as hostile input anyway.)
std::uint32_t checksum(const std::uint8_t* data, std::size_t size) {
  std::uint32_t hash = 2166136261u;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

void write_short_string(ByteWriter& writer, std::string_view text) {
  const std::size_t length = text.size() < kMaxSaveNameLength ? text.size() : kMaxSaveNameLength;
  writer.write_u8(static_cast<std::uint8_t>(length));
  writer.write_bytes(reinterpret_cast<const std::uint8_t*>(text.data()), length);
}

bool read_short_string(ByteReader& reader, std::string& out) {
  const std::uint8_t length = reader.read_u8();
  if (!reader.ok() || length == 0 || length > kMaxSaveNameLength) return false;
  const std::uint8_t* bytes = reader.skip(length);
  if (bytes == nullptr) return false;
  out.assign(reinterpret_cast<const char*>(bytes), length);
  return true;
}

// Names end up in messages on screen: printable ASCII only.
bool is_printable(const std::string& text) {
  for (char c : text) {
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

// Where the state starts and how long it is, once the header has been checked.
struct SaveLayout {
  std::size_t state_offset = 0;
  std::size_t state_size = 0;
};

bool check_save(const std::uint8_t* data, std::size_t size, SaveHeader& header, SaveLayout& layout,
                std::string& error) {
  if (data == nullptr || size == 0) {
    error = "the save file is empty";
    return false;
  }
  ByteReader reader(data, size);
  const std::uint32_t magic = reader.read_u32();
  if (!reader.ok() || magic != kSaveMagic) {
    error = "not a Scraps save file";
    return false;
  }
  header.version = reader.read_u16();
  if (!reader.ok()) {
    error = "the save file is truncated";
    return false;
  }
  if (header.version != kSaveFormatVersion) {
    error = "the save file has format version " + std::to_string(header.version) + "; this build reads version " +
            std::to_string(kSaveFormatVersion);
    return false;
  }

  // From here on, running out of bytes means a cut-off file; anything else
  // that does not add up means a damaged one.
  const bool game_ok = read_short_string(reader, header.game_name);
  const bool scene_ok = game_ok && read_short_string(reader, header.scene_name);
  header.scene_hash = reader.read_u32();
  header.tick = reader.read_u32();
  const std::uint32_t state_size = reader.read_u32();
  if (!reader.ok()) {
    error = "the save file is truncated";
    return false;
  }
  if (!game_ok || !scene_ok || !is_printable(header.game_name) || !is_valid_scene_name(header.scene_name)) {
    error = "the save file is corrupted (bad names in its header)";
    return false;
  }
  if (reader.remaining() < kChecksumSize || reader.remaining() - kChecksumSize < state_size) {
    error = "the save file is truncated";
    return false;
  }
  if (reader.remaining() - kChecksumSize > state_size) {
    error = "the save file is corrupted (unexpected bytes at its end)";
    return false;
  }
  layout.state_offset = size - reader.remaining();
  layout.state_size = state_size;

  ByteReader tail(data + size - kChecksumSize, kChecksumSize);
  if (tail.read_u32() != checksum(data, size - kChecksumSize)) {
    error = "the save file is corrupted (checksum mismatch)";
    return false;
  }
  return true;
}

}  // namespace

void write_save(const World& world, std::string_view scene_name, std::uint32_t scene_hash,
                std::string_view game_name, std::vector<std::uint8_t>& out) {
  out.clear();
  ByteWriter writer(out);
  writer.write_u32(kSaveMagic);
  writer.write_u16(kSaveFormatVersion);
  write_short_string(writer, game_name);
  write_short_string(writer, scene_name);
  writer.write_u32(scene_hash);
  writer.write_u32(world.tick);
  // The state's size goes before it: written as zero, filled in after.
  const std::size_t size_at = out.size();
  writer.write_u32(0);
  const std::size_t state_at = out.size();
  write_world_state(writer, world);
  const std::uint32_t state_size = static_cast<std::uint32_t>(out.size() - state_at);
  for (std::size_t i = 0; i < 4; ++i) out[size_at + i] = static_cast<std::uint8_t>(state_size >> (8 * i));
  writer.write_u32(checksum(out.data(), out.size()));
}

bool read_save_header(const std::uint8_t* data, std::size_t size, SaveHeader& header, std::string& error) {
  SaveLayout layout;
  return check_save(data, size, header, layout, error);
}

SaveLoad read_save(const std::uint8_t* data, std::size_t size, const SaveTarget& target, World& world_from_scene,
                   SaveHeader& header, std::string& error) {
  SaveLayout layout;
  if (!check_save(data, size, header, layout, error)) return SaveLoad::kRefused;
  if (header.game_name != target.game_name) {
    error = "this save belongs to the game '" + header.game_name + "', not '" + target.game_name + "'";
    return SaveLoad::kRefused;
  }
  if (header.scene_name != target.scene_name) {
    error = "this save is for the scene '" + header.scene_name + "', not '" + target.scene_name + "'";
    return SaveLoad::kRefused;
  }

  // Read into a copy, so a refused save leaves the caller's World as it was.
  World loaded = world_from_scene;
  ByteReader reader(data + layout.state_offset, layout.state_size);
  if (!read_world_state(reader, loaded) || reader.remaining() != 0) {
    error = "the save file is corrupted (its world does not fit the scene '" + target.scene_name + "')";
    return SaveLoad::kRefused;
  }
  if (loaded.tick != header.tick) {
    error = "the save file is corrupted (its header and its world disagree on the tick)";
    return SaveLoad::kRefused;
  }
  // make_world reserved room for spawning; a loaded world keeps that headroom
  // so play after a load allocates no more than play after a start.
  loaded.entities.reserve(world_from_scene.entities.capacity());
  loaded.free_slots.reserve(world_from_scene.free_slots.capacity());
  world_from_scene = std::move(loaded);
  error.clear();
  return header.scene_hash == target.scene_hash ? SaveLoad::kLoaded : SaveLoad::kSceneChanged;
}

}  // namespace scraps
