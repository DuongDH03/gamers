#pragma once

#include "views/view.h"

namespace scraps {

// Looking along +y, like a platformer. Screen x = world x, screen up = world +z.
// Gravity pulls along -z, jumping is allowed, y is locked (entities stay in
// their lane). Cells in front of the camera's lane are cut away so the player
// is never hidden; deeper lanes are drawn darker, as a backdrop.
class SideView final : public View {
 public:
  Perspective perspective() const override { return Perspective::kSide; }
  MovementRules movement_rules() const override;
  float default_zoom() const override { return 40.0f; }
  Vec2 world_to_screen(const Vec3& point, const Camera& camera) const override;
  Vec3 screen_to_world(Vec2 screen, const Camera& camera, const Vec3& through) const override;
  Vec2 screen_to_ground_direction(Vec2 screen_direction) const override;

 protected:
  void collect_blocks(const Level& level, const Camera& camera) override;
  void entity_depth(const Vec3& position, const Entity& entity, float& depth, float& tie) const override;
  void emit_block(const DrawItem& item, const Level& level, const Camera& camera) override;
  void emit_entity(const Vec3& position, const Entity& entity, const Camera& camera) override;
  void emit_shadow(const DrawItem& item, const Camera& camera) override;

 private:
  int m_lane = 0;  // first y layer drawn, from the camera target
};

}  // namespace scraps
