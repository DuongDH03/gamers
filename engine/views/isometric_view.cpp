#include "views/isometric_view.h"

namespace scraps {
namespace {

constexpr float kTopShade = 1.0f;
constexpr float kSideXShade = 0.78f;
constexpr float kSideYShade = 0.62f;

}  // namespace

MovementRules IsometricView::movement_rules() const {
  MovementRules rules;
  rules.gravity = Vec3(0.0f, 0.0f, -30.0f);
  rules.jump_speed = 9.5f;
  rules.move_x = true;
  rules.move_y = true;
  return rules;
}

Vec2 IsometricView::world_to_screen(const Vec3& point, const Camera& camera) const {
  const Vec3 d = point - camera.target;
  return Vec2((d.x - d.y) * camera.zoom * 0.5f + camera.viewport_width * 0.5f,
              (d.x + d.y) * camera.zoom * 0.25f - d.z * camera.zoom * 0.5f + camera.viewport_height * 0.5f);
}

Vec3 IsometricView::screen_to_world(Vec2 screen, const Camera& camera, const Vec3& through) const {
  if (!(camera.zoom > 0.0f)) return through;
  // Inverse of world_to_screen on the plane z = through.z. With d = point -
  // target and (dx, dy) the pixels from the viewport centre:
  //   dx = (d.x - d.y) zoom / 2              =>  a = d.x - d.y = 2 dx / zoom
  //   dy = (d.x + d.y) zoom / 4 - d.z zoom / 2 => b = d.x + d.y = 4 dy / zoom + 2 d.z
  const float dx = screen.x - camera.viewport_width * 0.5f;
  const float dy = screen.y - camera.viewport_height * 0.5f;
  const float dz = through.z - camera.target.z;
  const float a = 2.0f * dx / camera.zoom;
  const float b = 4.0f * dy / camera.zoom + 2.0f * dz;
  return Vec3(camera.target.x + (a + b) * 0.5f, camera.target.y + (b - a) * 0.5f, through.z);
}

Vec2 IsometricView::screen_to_ground_direction(Vec2 screen_direction) const {
  // Inverse of the ground part of world_to_screen (zoom cancels out):
  //   sx = (x - y) / 2,  sy = (x + y) / 4   =>   x = sx + 2 sy,  y = -sx + 2 sy
  const Vec2 s = screen_direction;
  return normalized(Vec2(s.x + 2.0f * s.y, -s.x + 2.0f * s.y));
}

void IsometricView::collect_blocks(const Level& level, const Camera& camera) {
  const TileGrid& grid = level.grid;
  const float half_width = camera.zoom * 0.5f;  // a cube's screen half-width
  for (int z = 0; z < grid.height; ++z) {
    for (int y = 0; y < grid.depth; ++y) {
      for (int x = 0; x < grid.width; ++x) {
        if (grid.get(x, y, z) == kEmptyTile) continue;
        const bool top = grid.get(x, y, z + 1) == kEmptyTile;
        const bool side_x = grid.get(x + 1, y, z) == kEmptyTile;
        const bool side_y = grid.get(x, y + 1, z) == kEmptyTile;
        if (!top && !side_x && !side_y) continue;  // completely hidden by its neighbours

        const Vec2 center = world_to_screen(
            Vec3(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f), camera);
        if (!on_screen(center.x - half_width, center.y - camera.zoom, center.x + half_width, center.y + camera.zoom,
                       camera)) {
          continue;
        }
        push_block(x, y, z, static_cast<float>(x + y + z) + 1.5f, 0.0f);  // sum at the cube center
      }
    }
  }
}

void IsometricView::entity_depth(const Vec3& position, const Entity&, float& depth, float& tie) const {
  // Bottom-center sum plus half a cell. Standing on cell (i, j, k-1) puts an
  // entity after that cell (i+j+k+0.5) and before the cells in front of it.
  // Entities that straddle cells can still sort wrongly against tall
  // neighbours; that is the classic limit of per-object painter's sorting.
  depth = position.x + position.y + position.z + 0.5f;
  tie = 1.0f;
}

void IsometricView::emit_block(const DrawItem& item, const Level& level, const Camera& camera) {
  const TileGrid& grid = level.grid;
  const TileId id = grid.get(item.x, item.y, item.z);
  if (id >= level.tiles.size()) return;
  const bool top = grid.get(item.x, item.y, item.z + 1) == kEmptyTile;
  const bool side_x = grid.get(item.x + 1, item.y, item.z) == kEmptyTile;
  const bool side_y = grid.get(item.x, item.y + 1, item.z) == kEmptyTile;
  const Vec3 min(static_cast<float>(item.x), static_cast<float>(item.y), static_cast<float>(item.z));
  push_box(min, min + Vec3(1.0f, 1.0f, 1.0f), level.tiles[id].color, top, side_x, side_y, camera,
           face_texture(level, item.x, item.y, item.z, true), face_texture(level, item.x, item.y, item.z, false));
}

void IsometricView::emit_entity(const Vec3& position, const Entity& entity, const Camera& camera) {
  // A sprite is an upright picture facing the camera, standing on the
  // entity's feet; its depth is the entity's, so blocks in front still cover it.
  if (push_sprite(entity, position, world_to_screen(position, camera), entity.size.x * camera.zoom * kSpriteScale,
                  camera)) {
    return;
  }
  const Vec3 half(entity.size.x * 0.5f, entity.size.y * 0.5f, 0.0f);
  const Vec3 min = position - half;
  const Vec3 max(position.x + half.x, position.y + half.y, position.z + entity.size.z);
  push_box(min, max, entity.color, true, true, true, camera, FaceTexture(), FaceTexture());
}

void IsometricView::push_box(const Vec3& min, const Vec3& max, Color color, bool top, bool side_x, bool side_y,
                             const Camera& camera, const FaceTexture& top_texture, const FaceTexture& side_texture) {
  auto p = [&](float x, float y, float z) { return world_to_screen(Vec3(x, y, z), camera); };
  // A textured face keeps its own colours and is only darkened; a flat face
  // uses the tile colour.
  auto face = [&](Vec2 a, Vec2 b, Vec2 c, Vec2 d, float shade, const FaceTexture& texture) {
    if (texture.texture != kNoTexture) {
      push_textured_quad(a, b, c, d, scale_rgb(Color(255, 255, 255), shade), texture);
    } else {
      push_quad(a, b, c, d, scale_rgb(color, shade));
    }
  };
  if (side_x) {
    face(p(max.x, min.y, max.z), p(max.x, max.y, max.z), p(max.x, max.y, min.z), p(max.x, min.y, min.z), kSideXShade,
         side_texture);
  }
  if (side_y) {
    face(p(min.x, max.y, max.z), p(max.x, max.y, max.z), p(max.x, max.y, min.z), p(min.x, max.y, min.z), kSideYShade,
         side_texture);
  }
  if (top) {
    face(p(min.x, min.y, max.z), p(max.x, min.y, max.z), p(max.x, max.y, max.z), p(min.x, max.y, max.z), kTopShade,
         top_texture);
  }
}

}  // namespace scraps
