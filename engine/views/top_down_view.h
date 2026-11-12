#pragma once

#include "views/view.h"

namespace scraps {

// Looking straight down the -z axis. Screen x = world x, screen y = world y.
// No gravity, no jumping; free movement on the ground plane. Things with a
// higher top surface are painted later (they are closer to the camera).
class TopDownView final : public View {
 public:
  Perspective perspective() const override { return Perspective::kTopDown; }
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
  void shadow_depth(const Vec3& ground, const Entity& entity, float& depth, float& tie) const override;
};

}  // namespace scraps
