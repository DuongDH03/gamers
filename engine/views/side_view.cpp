#include "views/side_view.h"

#include <algorithm>
#include <cmath>

namespace scraps {

MovementRules SideView::movement_rules() const {
  MovementRules rules;
  rules.gravity = Vec3(0.0f, 0.0f, -30.0f);
  rules.jump_speed = 11.5f;
  rules.move_x = true;
  rules.move_y = false;
  return rules;
}

Vec2 SideView::world_to_screen(const Vec3& point, const Camera& camera) const {
  return Vec2((point.x - camera.target.x) * camera.zoom + camera.viewport_width * 0.5f,
              -(point.z - camera.target.z) * camera.zoom + camera.viewport_height * 0.5f);
}

Vec3 SideView::screen_to_world(Vec2 screen, const Camera& camera, const Vec3& through) const {
  if (!(camera.zoom > 0.0f)) return through;
  // The plane y = through.y (the lane); screen down is world -z.
  return Vec3(camera.target.x + (screen.x - camera.viewport_width * 0.5f) / camera.zoom, through.y,
              camera.target.z - (screen.y - camera.viewport_height * 0.5f) / camera.zoom);
}

Vec2 SideView::screen_to_ground_direction(Vec2 screen_direction) const {
  // Only left/right means anything on the ground; up and down are not ground directions here.
  return normalized(Vec2(screen_direction.x, 0.0f));
}

void SideView::collect_blocks(const Level& level, const Camera& camera) {
  const TileGrid& grid = level.grid;
  if (grid.depth <= 0) return;
  m_lane = std::max(0, std::min(grid.depth - 1, static_cast<int>(std::floor(camera.target.y))));

  const float half_w = camera.viewport_width * 0.5f / camera.zoom;
  const float half_h = camera.viewport_height * 0.5f / camera.zoom;
  const int x0 = std::max(0, static_cast<int>(std::floor(camera.target.x - half_w)) - 1);
  const int x1 = std::min(grid.width - 1, static_cast<int>(std::ceil(camera.target.x + half_w)) + 1);
  const int z0 = std::max(0, static_cast<int>(std::floor(camera.target.z - half_h)) - 1);
  const int z1 = std::min(grid.height - 1, static_cast<int>(std::ceil(camera.target.z + half_h)) + 1);

  for (int z = z0; z <= z1; ++z) {
    for (int x = x0; x <= x1; ++x) {
      // Only the front-most cell at or behind the lane is visible.
      for (int y = m_lane; y < grid.depth; ++y) {
        if (grid.get(x, y, z) == kEmptyTile) continue;
        // Farther lanes first: depth decreases as y grows.
        push_block(x, y, z, -(static_cast<float>(y) + 0.5f), 0.0f);
        break;
      }
    }
  }
}

void SideView::entity_depth(const Vec3& position, const Entity&, float& depth, float& tie) const {
  depth = -position.y;
  tie = 1.0f;  // in front of a block at the same depth
}

void SideView::emit_block(const DrawItem& item, const Level& level, const Camera& camera) {
  const TileId id = level.grid.get(item.x, item.y, item.z);
  if (id >= level.tiles.size()) return;
  // Farther lanes are a little darker, enough to read as background without
  // turning light things (clouds) grey.
  const float shade = std::max(0.6f, 1.0f - 0.12f * static_cast<float>(item.y - m_lane));
  // Screen rectangle from the cell's top-left corner (x, z + 1).
  const Vec2 corner =
      world_to_screen(Vec3(static_cast<float>(item.x), 0.0f, static_cast<float>(item.z + 1)), camera);
  // The side view shows a block's front face, a side face.
  const FaceTexture face = face_texture(level, item.x, item.y, item.z, false);
  if (face.texture != kNoTexture) {
    const float s = camera.zoom;
    push_textured_quad(corner, Vec2(corner.x + s, corner.y), Vec2(corner.x + s, corner.y + s),
                       Vec2(corner.x, corner.y + s), scale_rgb(Color(255, 255, 255), shade), face);
  } else {
    push_rect(corner.x, corner.y, camera.zoom, camera.zoom, scale_rgb(level.tiles[id].color, shade));
  }
}

void SideView::emit_shadow(const DrawItem& item, const Camera& camera) {
  // Seen from the side the ground is a line, so the shadow is a flat ellipse
  // lying on it.
  const Vec2 center = world_to_screen(item.position, camera);
  const float rx = item.radius * camera.zoom;
  push_ellipse(center, rx, rx * 0.22f, Color(0, 0, 0, static_cast<std::uint8_t>(90.0f * item.strength)));
}

void SideView::emit_entity(const Vec3& position, const Entity& entity, const Camera& camera) {
  if (push_sprite(entity, position, world_to_screen(position, camera), entity.size.x * camera.zoom * kSpriteScale,
                  camera)) {
    return;
  }
  const Vec2 top_left = world_to_screen(Vec3(position.x - entity.size.x * 0.5f, 0.0f, position.z + entity.size.z), camera);
  const float w = entity.size.x * camera.zoom;
  const float h = entity.size.z * camera.zoom;
  push_rect(top_left.x, top_left.y, w, h, scale_rgb(entity.color, 0.55f));
  const float inset = std::min(w, h) * 0.14f;
  push_rect(top_left.x + inset, top_left.y + inset, w - 2.0f * inset, h - 2.0f * inset, entity.color);
}

}  // namespace scraps
