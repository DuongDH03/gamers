#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/entity.h"
#include "core/level.h"
#include "core/movement.h"
#include "core/tick.h"

namespace scraps {

// The whole game state. Plain, copyable data (Rule 5): copying a World is how
// we keep the previous tick for interpolation and how the client rewinds for
// prediction. Serializable without special cases (core/serialize.h).
//
// Only simulate() may change a running World (Rule 1). The helpers below that
// take a `World&` exist for simulate and for building the initial world.
struct World {
  Tick tick = 0;
  std::uint64_t rng_state = 0x9E3779B97F4A7C15ull;
  MovementRules rules;
  Level level;
  std::vector<Entity> entities;           // slots; dead slots have alive == false
  std::vector<std::uint32_t> free_slots;  // dead slot indices, reused last-in first-out
  std::array<EntityId, kMaxPlayers> player_entities{};
};

// The part of a World that a network snapshot carries: everything but the
// rules and the Level, which both sides load from the same scene file. The
// server keeps recent ones as delta baselines, and so does the client
// (core/serialize.h, write_world_delta). Plain data, like World.
struct WorldState {
  Tick tick = 0;
  std::uint64_t rng_state = 0;
  std::vector<Entity> entities;
  std::vector<std::uint32_t> free_slots;
  std::array<EntityId, kMaxPlayers> player_entities{};
};

// Copies the state of `world` into `out`. Allocates nothing once `out`'s
// vectors have the capacity (reserve them up front).
void copy_state(const World& world, WorldState& out);
// Reserves room for `entity_capacity` slots in both of `state`'s vectors.
void reserve_state(WorldState& state, std::size_t entity_capacity);

// Spawns an entity from an archetype. Returns an invalid id if the archetype
// does not exist or the world is full. `position` is taken by value: passing
// another entity's position is safe even if spawning grows the entity array.
EntityId spawn_entity(World& world, std::uint16_t archetype, Vec3 position);
// Returns false if `id` does not refer to a live entity.
bool destroy_entity(World& world, EntityId id);

// Returns nullptr if `id` does not refer to a live entity. Do not keep the
// pointer: it is invalidated when entities are spawned.
Entity* find_entity(World& world, EntityId id);
const Entity* find_entity(const World& world, EntityId id);

// The handle of the entity currently in slot `index`.
EntityId entity_id_at(const World& world, std::uint32_t index);
EntityId player_entity(const World& world, PlayerId player);
std::size_t count_alive(const World& world);

// True if the boxes of two entities overlap.
bool entities_overlap(const Entity& a, const Entity& b);

// Deterministic random numbers stored in the world, so they copy, serialize
// and replay with it.
std::uint32_t next_random(World& world);
float random_unit(World& world);  // [0, 1)

}  // namespace scraps
