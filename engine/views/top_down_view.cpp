#include "views/top_down_view.h"

#include <algorithm>
#include <cmath>

namespace scraps {
namespace {

// Height index of the highest non-empty cell in a column, or -1.
int column_top(const TileGrid& grid, int x, int y) {
  for (int z = grid.height - 1; z >= 0; --z) {
    if (grid.get(x, y, z) != kEmptyTile) return z;
  }
  return -1;
}

}  // namespace

MovementRules TopDownView::movement_rules() const {
  MovementRules rules;
  rules.gravity = Vec3(0.0f, 0.0f, 0.0f);
  rules.jump_speed = 0.0f;
  rules.move_x = true;
  rules.move_y = true;
  return rules;
}

Vec2 TopDownView::world_to_screen(const Vec3& point, const Camera& camera) const {
  return Vec2((point.x - camera.target.x) * camera.zoom + camera.viewport_width * 0.5f,
              (point.y - camera.target.y) * camera.zoom + camera.viewport_height * 0.5f);
}

Vec3 TopDownView::screen_to_world(Vec2 screen, const Camera& camera, const Vec3& through) const {
  if (!(camera.zoom > 0.0f)) return through;
  // The plane z = through.z; on it, world_to_screen is a scale and a shift.
  return Vec3(camera.target.x + (screen.x - camera.viewport_width * 0.5f) / camera.zoom,
              camera.target.y + (screen.y - camera.viewport_height * 0.5f) / camera.zoom, through.z);
}

Vec2 TopDownView::screen_to_ground_direction(Vec2 screen_direction) const { return normalized(screen_direction); }

void TopDownView::collect_blocks(const Level& level, const Camera& camera) {
  const TileGrid& grid = level.grid;
  const float half_w = camera.viewport_width * 0.5f / camera.zoom;
  const float half_h = camera.viewport_height * 0.5f / camera.zoom;
  const int x0 = std::max(0, static_cast<int>(std::floor(camera.target.x - half_w)) - 1);
  const int x1 = std::min(grid.width - 1, static_cast<int>(std::ceil(camera.target.x + half_w)) + 1);
  const int y0 = std::max(0, static_cast<int>(std::floor(camera.target.y - half_h)) - 1);
  const int y1 = std::min(grid.depth - 1, static_cast<int>(std::ceil(camera.target.y + half_h)) + 1);

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const int z = column_top(grid, x, y);
      if (z < 0) continue;
      // Depth = height of the top surface: higher things are painted later.
      push_block(x, y, z, static_cast<float>(z + 1), 0.0f);
    }
  }
}

void TopDownView::entity_depth(const Vec3& position, const Entity& entity, float& depth, float& tie) const {
  depth = position.z + entity.size.z;
  tie = position.y;
}

void TopDownView::shadow_depth(const Vec3& ground, const Entity&, float& depth, float& tie) const {
  // Just above the ground's top surface and below anything standing on it:
  // every shadow on one floor is painted in a row, as one batch.
  depth = ground.z + 0.001f;
  tie = 0.0f;
}

void TopDownView::emit_block(const DrawItem& item, const Level& level, const Camera& camera) {
  const TileGrid& grid = level.grid;
  const TileId id = grid.get(item.x, item.y, item.z);
  if (id >= level.tiles.size()) return;

  // Higher cells are brighter; a light checker pattern makes motion visible.
  const float height_shade = 0.6f + 0.4f * static_cast<float>(item.z + 1) / static_cast<float>(grid.height);
  const Vec2 corner = world_to_screen(Vec3(static_cast<float>(item.x), static_cast<float>(item.y), 0.0f), camera);

  const FaceTexture face = face_texture(level, item.x, item.y, item.z, true);
  if (face.texture != kNoTexture) {
    const float s = camera.zoom;
    push_textured_quad(corner, Vec2(corner.x + s, corner.y), Vec2(corner.x + s, corner.y + s),
                       Vec2(corner.x, corner.y + s), scale_rgb(Color(255, 255, 255), height_shade), face);
  } else {
    const float checker = ((item.x + item.y) & 1) != 0 ? 0.94f : 1.0f;
    push_rect(corner.x, corner.y, camera.zoom, camera.zoom, scale_rgb(level.tiles[id].color, height_shade * checker));
  }

  // A shadow strip on the south edge when the cell below on screen is lower:
  // the only height cue a straight-down camera has.
  if (column_top(grid, item.x, item.y + 1) < item.z && item.y + 1 < grid.depth) {
    const float strip = camera.zoom * 0.18f;
    push_rect(corner.x, corner.y + camera.zoom, camera.zoom, strip, Color(0, 0, 0, 70));
  }
}

void TopDownView::emit_entity(const Vec3& position, const Entity& entity, const Camera& camera) {
  // A sprite stands on the south edge of the entity's footprint, so the
  // picture rises "up" the screen like a 3/4-view character.
  const Vec2 feet = world_to_screen(position + Vec3(0.0f, entity.size.y * 0.5f, 0.0f), camera);
  const float sprite_width = std::max(entity.size.x, entity.size.y) * camera.zoom * kSpriteScale;
  if (push_sprite(entity, position, feet, sprite_width, camera)) return;

  const Vec2 center = world_to_screen(position, camera);
  const float w = entity.size.x * camera.zoom;
  const float h = entity.size.y * camera.zoom;
  push_rect(center.x - w * 0.5f, center.y - h * 0.5f, w, h, scale_rgb(entity.color, 0.55f));
  const float inset = std::min(w, h) * 0.14f;
  push_rect(center.x - w * 0.5f + inset, center.y - h * 0.5f + inset, w - 2.0f * inset, h - 2.0f * inset,
            entity.color);
}

}  // namespace scraps
