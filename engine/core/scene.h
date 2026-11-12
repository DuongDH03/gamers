#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/level.h"
#include "core/math.h"
#include "core/movement.h"
#include "core/world.h"

namespace scraps {

struct SceneEntity {
  std::uint16_t archetype = 0;
  Vec3 position;
};

struct SceneCrowd {
  std::uint16_t archetype = 0;
  std::uint32_t count = 0;
};

// A level as loaded from a .scene text file (format: docs/scene-format.md).
struct Scene {
  std::string name;                // file name without directory and extension
  std::uint32_t content_hash = 0;  // lets client and server check they loaded the same file
  Level level;
  std::vector<SceneEntity> entities;
  std::vector<SceneCrowd> crowds;  // entities scattered at random when the world is made
};

// Most entities a scene may place (entity lines plus crowds). It keeps a full
// snapshot (about 85 bytes per entity) well under kMaxSnapshotSize, with room
// for entities spawned during play.
constexpr std::uint32_t kMaxSceneEntities = 4096;

// FNV-1a hash of the text, ignoring '\r' so CRLF and LF checkouts match.
std::uint32_t hash_scene_text(std::string_view text);

// Scene names travel over the network and become file names, so only
// letters, digits, '-' and '_' are allowed (no paths).
bool is_valid_scene_name(std::string_view name);

// Parses scene text. On failure returns false and sets `error` to a message
// with the line number.
bool parse_scene(std::string_view text, Scene& out, std::string& error);
bool load_scene_file(const std::string& path, Scene& out, std::string& error);
// Loads "<directory>/<name>.scene".
bool load_scene(const std::string& directory, std::string_view name, Scene& out, std::string& error);

// Builds the initial World for a scene. This is construction, not mutation:
// after this only simulate() changes the world (Rule 1).
World make_world(const Scene& scene, const MovementRules& rules);

}  // namespace scraps
