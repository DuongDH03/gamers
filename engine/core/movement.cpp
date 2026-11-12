#include "core/movement.h"

#include <algorithm>
#include <cmath>

#include "core/jobs.h"
#include "core/world.h"

namespace scraps {
namespace {

// Entities per job chunk: big enough that one chunk outweighs the cost of
// handing it to a thread, small enough to spread a 1,000-entity world.
constexpr std::size_t kEntitiesPerJob = 128;

// Gap kept between a resolved box and the cell it touched, so that the next
// overlap test does not count "exactly touching" as overlapping.
constexpr float kSkin = 1e-4f;
// Largest move per collision sub-step. Below one cell, so nothing tunnels.
constexpr float kMaxSubStep = 0.45f;
constexpr int kMaxSubSteps = 8;
// Coordinates are clamped before converting to int, so a runaway entity can
// never cause an overflowing float-to-int conversion.
constexpr float kCoordinateLimit = 1.0e6f;

struct Box {
  Vec3 min;
  Vec3 max;
};

Box entity_box(const Vec3& position, const Vec3& size) {
  Box box;
  box.min = Vec3(position.x - size.x * 0.5f, position.y - size.y * 0.5f, position.z);
  box.max = Vec3(position.x + size.x * 0.5f, position.y + size.y * 0.5f, position.z + size.z);
  return box;
}

float component(const Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }
float& component(Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

float clamp_coordinate(float value) {
  if (!std::isfinite(value)) return 0.0f;
  return std::max(-kCoordinateLimit, std::min(kCoordinateLimit, value));
}
// First cell index touched by a box edge at `value` (min side).
int first_cell(float value) { return static_cast<int>(std::floor(clamp_coordinate(value))); }
// Last cell index touched by a box edge at `value` (max side). A box ending
// exactly on a cell boundary does not touch the next cell.
int last_cell(float value) { return static_cast<int>(std::ceil(clamp_coordinate(value))) - 1; }

// Finds the solid cells overlapping `box`. Returns false if there are none;
// otherwise `lowest`/`highest` are their smallest/largest index along `axis`.
bool solid_cells_along(const Level& level, const Box& box, int axis, int& lowest, int& highest) {
  const int x0 = first_cell(box.min.x), x1 = last_cell(box.max.x);
  const int y0 = first_cell(box.min.y), y1 = last_cell(box.max.y);
  const int z0 = first_cell(box.min.z), z1 = last_cell(box.max.z);
  bool found = false;
  for (int z = z0; z <= z1; ++z) {
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        if (!is_solid_cell(level, x, y, z)) continue;
        const int c = axis == 0 ? x : (axis == 1 ? y : z);
        if (!found) {
          lowest = c;
          highest = c;
          found = true;
        } else {
          lowest = std::min(lowest, c);
          highest = std::max(highest, c);
        }
      }
    }
  }
  return found;
}

// Moves `entity` by `delta` along one axis and pushes it back out of any solid
// cell it entered. Resolving one axis at a time is what lets an entity slide
// along a wall instead of sticking to it.
void move_along_axis(const Level& level, Entity& entity, int axis, float delta) {
  if (delta == 0.0f) return;

  const Box before = entity_box(entity.position, entity.size);
  const bool started_inside = box_overlaps_solid(level, before.min, before.max);
  component(entity.position, axis) += delta;
  // An entity spawned inside geometry is allowed to move out of it.
  if (started_inside) return;

  int lowest = 0;
  int highest = 0;
  const Box after = entity_box(entity.position, entity.size);
  if (!solid_cells_along(level, after, axis, lowest, highest)) return;

  const float half = component(entity.size, axis) * 0.5f;
  if (delta > 0.0f) {
    // Leading face is the max face; put it just before the first blocking cell.
    const float extent_above = axis == 2 ? entity.size.z : half;
    component(entity.position, axis) = static_cast<float>(lowest) - extent_above - kSkin;
  } else {
    const float extent_below = axis == 2 ? 0.0f : half;
    component(entity.position, axis) = static_cast<float>(highest + 1) + extent_below + kSkin;
    if (axis == 2) entity.on_ground = true;
  }

  float& velocity = component(entity.velocity, axis);
  velocity = (entity.flags & kEntityBounces) != 0 ? -velocity : 0.0f;
}

void move_entity(const Level& level, Entity& entity, Vec3 delta) {
  if ((entity.flags & kEntityGravity) != 0) entity.on_ground = false;

  if ((entity.flags & kEntityCollides) == 0) {
    entity.position += delta;
    return;
  }

  const float limit = kMaxSubStep * static_cast<float>(kMaxSubSteps);
  delta.x = std::max(-limit, std::min(limit, delta.x));
  delta.y = std::max(-limit, std::min(limit, delta.y));
  delta.z = std::max(-limit, std::min(limit, delta.z));

  const float largest = std::max(std::fabs(delta.x), std::max(std::fabs(delta.y), std::fabs(delta.z)));
  int steps = static_cast<int>(std::ceil(largest / kMaxSubStep));
  steps = std::max(1, std::min(kMaxSubSteps, steps));
  const Vec3 step = delta * (1.0f / static_cast<float>(steps));

  for (int i = 0; i < steps; ++i) {
    for (int axis = 0; axis < 3; ++axis) {
      move_along_axis(level, entity, axis, component(step, axis));
    }
  }
}

}  // namespace

bool box_overlaps_solid(const Level& level, const Vec3& min, const Vec3& max) {
  Box box;
  box.min = min;
  box.max = max;
  int lowest = 0;
  int highest = 0;
  return solid_cells_along(level, box, 0, lowest, highest);
}

void update_facing(Entity& entity) {
  // A player faces where they ask to go; anything else, where it is going.
  // Standing still changes nothing, which is the point.
  Vec2 direction(entity.velocity.x, entity.velocity.y);
  if (entity.player != kNoPlayer) direction = entity.move_intent;
  if (direction.x * direction.x + direction.y * direction.y > 0.0025f) entity.facing = normalized(direction);
}

void step_movement(World& world, float dt, JobSystem* jobs) {
  const MovementRules& rules = world.rules;
  const Level& level = world.level;
  std::vector<Entity>& entities = world.entities;

  // Phase 1 (parallel-safe): every entity moves itself.
  auto move_range = [&](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
      Entity& entity = entities[i];
      if (!entity.alive || (entity.flags & kEntityMoves) == 0) continue;
      if (entity.player != kNoPlayer) {
        entity.velocity.x = entity.move_intent.x * entity.move_speed;
        entity.velocity.y = entity.move_intent.y * entity.move_speed;
      }
      if (!rules.move_x) entity.velocity.x = 0.0f;
      if (!rules.move_y) entity.velocity.y = 0.0f;
      update_facing(entity);
      if ((entity.flags & kEntityGravity) != 0) entity.velocity += rules.gravity * dt;
      move_entity(level, entity, entity.velocity * dt);
    }
  };
  if (jobs != nullptr) {
    jobs->parallel_for(entities.size(), kEntitiesPerJob, move_range);
  } else {
    move_range(0, entities.size());
  }

  // Phase 2 (one thread): fallen or lost entities. Destroying touches the
  // free list and player table, which are shared.
  for (std::size_t i = 0; i < entities.size(); ++i) {
    Entity& entity = entities[i];
    if (!entity.alive || (entity.flags & kEntityMoves) == 0) continue;
    const Vec3& p = entity.position;
    const bool lost = std::fabs(p.x) > kWorldExtent || std::fabs(p.y) > kWorldExtent || p.z > kWorldExtent;
    if (p.z < rules.kill_z || lost) {
      if (entity.player != kNoPlayer) {
        entity.position = spawn_point_for(world.level, entity.player);
        entity.velocity = Vec3();
      } else {
        destroy_entity(world, entity_id_at(world, static_cast<std::uint32_t>(i)));
      }
    }
  }
}

}  // namespace scraps
