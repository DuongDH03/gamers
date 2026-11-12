#pragma once

#include "core/math.h"

namespace scraps {

struct Entity;
struct Level;
struct World;
class JobSystem;

// How things move. A View decides these values (Rule 4); core only applies
// them, so core never assumes which way is down or which axes are allowed.
struct MovementRules {
  Vec3 gravity;             // acceleration in world units / s^2; zero = none
  float jump_speed = 0.0f;  // upward (+z) speed of a jump; 0 = no jumping
  bool move_x = true;       // may entities move along x?
  bool move_y = true;       // may entities move along y?
  float kill_z = -8.0f;     // below this height players respawn, others are removed
};

// The playable universe: anything farther than this from the origin on any
// axis is treated like falling below kill_z. It keeps every coordinate small
// enough to convert to int and to pass snapshot validation.
constexpr float kWorldExtent = 100000.0f;

// Points `facing` along the entity's intent (players) or velocity (anything
// else), when there is one. Called by step_movement.
void update_facing(Entity& entity);

// Moves every entity with kEntityMoves by one step of `dt` seconds: applies
// player intent, updates facing, applies gravity, then tile collision one
// axis at a time.
//
// With `jobs`, entities are split across threads. That is safe because each
// entity's step reads only the level (unchanged during a tick) and writes
// only that entity. Removing fallen entities touches shared lists, so it
// runs afterwards on the calling thread, in index order: the result is
// byte-for-byte the same as without `jobs`.
void step_movement(World& world, float dt, JobSystem* jobs = nullptr);

// True if the axis-aligned box [min, max] overlaps any solid cell.
bool box_overlaps_solid(const Level& level, const Vec3& min, const Vec3& max);

}  // namespace scraps
