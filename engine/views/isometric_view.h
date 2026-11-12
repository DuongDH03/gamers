#pragma once

#include "views/view.h"

namespace scraps {

// 2:1 "diamond" isometric. The camera looks along the world diagonal (1, 1, 1):
//
//   screen_x = (x - y) * zoom / 2
//   screen_y = (x + y) * zoom / 4  -  z * zoom / 2
//
// Moving by (1, 1, 1) changes neither screen coordinate, so (1, 1, 1) is the
// viewing direction and depth = x + y + z: a larger sum is closer to the
// viewer. Unit cubes with the same sum never overlap on screen, so sorting
// blocks by that sum paints them correctly. Gravity pulls along -z.
class IsometricView final : public View {
 public:
  Perspective perspective() const override { return Perspective::kIsometric; }
  MovementRules movement_rules() const override;
  float default_zoom() const override { return 56.0f; }
  Vec2 world_to_screen(const Vec3& point, const Camera& camera) const override;
  Vec3 screen_to_world(Vec2 screen, const Camera& camera, const Vec3& through) const override;
  Vec2 screen_to_ground_direction(Vec2 screen_direction) const override;

 protected:
  void collect_blocks(const Level& level, const Camera& camera) override;
  void entity_depth(const Vec3& position, const Entity& entity, float& depth, float& tie) const override;
  void emit_block(const DrawItem& item, const Level& level, const Camera& camera) override;
  void emit_entity(const Vec3& position, const Entity& entity, const Camera& camera) override;

 private:
  // Draws the three visible faces of the box [min, max]: top, +x side, +y side,
  // textured when a FaceTexture has a texture, else flat `color`.
  void push_box(const Vec3& min, const Vec3& max, Color color, bool top, bool side_x, bool side_y,
                const Camera& camera, const FaceTexture& top_texture, const FaceTexture& side_texture);
};

}  // namespace scraps
