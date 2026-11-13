#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/tick.h"
#include "core/world.h"

// Save files (docs/specs/2026-11-14-offline-essentials-and-ui.md, part 1).
//
// A save is the World's running state, exactly as a full snapshot carries it
// (write_world_state), behind a small header. The level and the rules are not
// stored: they come from the scene file, as on every start, and the saved
// state is laid over the World built from it.
//
//   magic "SCRS" u32 | format version u16 | game name | scene name
//   | scene content hash u32 | tick u32 | state size u32 | state
//   | checksum u32 (FNV-1a of every byte before it)
//
// Names are a u8 length and that many bytes. Core only turns bytes into a
// World and back; where the file lives is the app's choice (Rule 3).
namespace scraps {

constexpr std::uint32_t kSaveMagic = 0x53524353u;  // "SCRS" in little-endian bytes
constexpr std::uint16_t kSaveFormatVersion = 1;
constexpr std::size_t kMaxSaveNameLength = 63;
// Far above the largest World (kMaxEntities slots of about 85 bytes), so a
// file bigger than this is not a save, and nothing reads it whole.
constexpr std::size_t kMaxSaveSize = 64u << 20;

// What a save says about itself.
struct SaveHeader {
  std::uint16_t version = 0;
  std::string game_name;
  std::string scene_name;
  std::uint32_t scene_hash = 0;  // content hash of the scene file when it was saved
  Tick tick = 0;
};

// The game and scene a save must belong to: the ones running now.
struct SaveTarget {
  std::string game_name;
  std::string scene_name;
  std::uint32_t scene_hash = 0;  // of the scene file as it is now
};

enum class SaveLoad : std::uint8_t {
  kRefused,       // `error` says why; the World was not touched
  kLoaded,
  kSceneChanged,  // loaded, but the scene file changed since the save: warn the player
};

// Replaces `out` with a save of `world`. Allocates (not for the hot path).
// Names longer than kMaxSaveNameLength are cut.
void write_save(const World& world, std::string_view scene_name, std::uint32_t scene_hash,
                std::string_view game_name, std::vector<std::uint8_t>& out);

// Reads and checks only the header (and that the file is whole and
// unchanged), so an app can learn which scene a save needs before it loads
// that scene. Returns false with a message for a file that is not a save,
// another format version, a truncated or a corrupted one.
bool read_save_header(const std::uint8_t* data, std::size_t size, SaveHeader& header, std::string& error);

// Lays a save over `world_from_scene`, the World freshly built from the
// target's scene (its rules and level stay). Refuses, with a message and
// without touching the World, everything read_save_header refuses, a save
// of another game or another scene, and an invalid state. A save of the
// same scene whose file has changed since (another hash) is loaded, and says
// so with kSceneChanged: levels change during development.
SaveLoad read_save(const std::uint8_t* data, std::size_t size, const SaveTarget& target, World& world_from_scene,
                   SaveHeader& header, std::string& error);

}  // namespace scraps
