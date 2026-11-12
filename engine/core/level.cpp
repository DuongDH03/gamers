#include "core/level.h"

#include <cmath>

namespace scraps {

const char* perspective_name(Perspective perspective) {
  switch (perspective) {
    case Perspective::kTopDown: return "top-down";
    case Perspective::kSide: return "side";
    case Perspective::kIsometric: return "isometric";
    default: return "unknown";
  }
}

bool parse_perspective(std::string_view text, Perspective& out) {
  if (text == "top-down") {
    out = Perspective::kTopDown;
  } else if (text == "side") {
    out = Perspective::kSide;
  } else if (text == "isometric") {
    out = Perspective::kIsometric;
  } else {
    return false;
  }
  return true;
}

Name make_name(std::string_view text) {
  Name name{};
  const std::size_t count = text.size() < kNameCapacity - 1 ? text.size() : kNameCapacity - 1;
  for (std::size_t i = 0; i < count; ++i) name[i] = text[i];
  return name;
}

std::string_view name_view(const Name& name) {
  std::size_t length = 0;
  while (length < name.size() && name[length] != '\0') ++length;
  return std::string_view(name.data(), length);
}

AssetPath make_asset_path(std::string_view text) {
  AssetPath path{};
  const std::size_t count = text.size() < kPathCapacity - 1 ? text.size() : kPathCapacity - 1;
  for (std::size_t i = 0; i < count; ++i) path[i] = text[i];
  return path;
}

std::string_view path_view(const AssetPath& path) {
  std::size_t length = 0;
  while (length < path.size() && path[length] != '\0') ++length;
  return std::string_view(path.data(), length);
}

bool is_valid_asset_path(std::string_view path) {
  if (path.empty() || path.size() >= kPathCapacity || path.front() == '/') return false;
  if (path.find("..") != std::string_view::npos) return false;
  for (char c : path) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                    c == '_' || c == '.' || c == '/';
    if (!ok) return false;
  }
  return true;
}

int find_sound(const Level& level, std::string_view name) {
  for (std::size_t i = 0; i < level.sounds.size(); ++i) {
    if (name_view(level.sounds[i].name) == name) return static_cast<int>(i);
  }
  return -1;
}

int find_image(const Level& level, std::string_view name) {
  for (std::size_t i = 0; i < level.images.size(); ++i) {
    if (name_view(level.images[i].name) == name) return static_cast<int>(i);
  }
  return -1;
}

bool is_solid_cell(const Level& level, int x, int y, int z) {
  const TileGrid& grid = level.grid;
  if (x < 0 || y < 0 || x >= grid.width || y >= grid.depth) return true;
  if (z < 0 || z >= grid.height) return false;
  const TileId id = grid.cells[grid.index(x, y, z)];
  if (id == kEmptyTile || id >= level.tiles.size()) return false;
  return level.tiles[id].solid;
}

bool cell_at_point(const Vec3& point, CellCoord& out) {
  if (!is_finite(point) || std::fabs(point.x) > kMaxCellCoordinate || std::fabs(point.y) > kMaxCellCoordinate ||
      std::fabs(point.z) > kMaxCellCoordinate) {
    return false;
  }
  out.x = static_cast<int>(std::floor(point.x));
  out.y = static_cast<int>(std::floor(point.y));
  out.z = static_cast<int>(std::floor(point.z + 1e-3f));
  return true;
}

int find_archetype(const Level& level, std::string_view name) {
  for (std::size_t i = 0; i < level.archetypes.size(); ++i) {
    if (name_view(level.archetypes[i].name) == name) return static_cast<int>(i);
  }
  return -1;
}

Vec3 spawn_point_for(const Level& level, std::uint8_t player) {
  if (level.spawn_points.empty()) {
    return Vec3(static_cast<float>(level.grid.width) * 0.5f, static_cast<float>(level.grid.depth) * 0.5f,
                static_cast<float>(level.grid.height));
  }
  return level.spawn_points[player % level.spawn_points.size()];
}

}  // namespace scraps
