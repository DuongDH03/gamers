#include "core/world.h"

namespace scraps {

EntityId spawn_entity(World& world, std::uint16_t archetype, Vec3 position) {
  if (archetype >= world.level.archetypes.size()) return EntityId();

  std::uint32_t index = 0;
  if (!world.free_slots.empty()) {
    index = world.free_slots.back();
    world.free_slots.pop_back();
  } else {
    if (world.entities.size() >= kMaxEntities) return EntityId();
    index = static_cast<std::uint32_t>(world.entities.size());
    world.entities.emplace_back();
  }

  const Archetype& recipe = world.level.archetypes[archetype];
  Entity& entity = world.entities[index];
  const std::uint32_t generation = entity.generation;
  entity = Entity();
  entity.generation = generation;
  entity.alive = true;
  entity.archetype = archetype;
  entity.kind = recipe.kind;
  entity.flags = recipe.flags;
  entity.position = position;
  entity.size = recipe.size;
  entity.color = recipe.color;
  entity.move_speed = recipe.move_speed;
  return EntityId(index, generation);
}

void copy_state(const World& world, WorldState& out) {
  out.tick = world.tick;
  out.rng_state = world.rng_state;
  out.entities = world.entities;      // vector assignment reuses capacity
  out.free_slots = world.free_slots;
  out.player_entities = world.player_entities;
}

void reserve_state(WorldState& state, std::size_t entity_capacity) {
  state.entities.reserve(entity_capacity);
  state.free_slots.reserve(entity_capacity);
}

bool destroy_entity(World& world, EntityId id) {
  Entity* entity = find_entity(world, id);
  if (entity == nullptr) return false;
  if (entity->player != kNoPlayer && entity->player < kMaxPlayers &&
      world.player_entities[entity->player] == id) {
    world.player_entities[entity->player] = EntityId();
  }
  entity->alive = false;
  ++entity->generation;
  world.free_slots.push_back(id.index);
  return true;
}

Entity* find_entity(World& world, EntityId id) {
  if (!id.is_valid() || id.index >= world.entities.size()) return nullptr;
  Entity& entity = world.entities[id.index];
  if (!entity.alive || entity.generation != id.generation) return nullptr;
  return &entity;
}

const Entity* find_entity(const World& world, EntityId id) {
  if (!id.is_valid() || id.index >= world.entities.size()) return nullptr;
  const Entity& entity = world.entities[id.index];
  if (!entity.alive || entity.generation != id.generation) return nullptr;
  return &entity;
}

EntityId entity_id_at(const World& world, std::uint32_t index) {
  if (index >= world.entities.size()) return EntityId();
  return EntityId(index, world.entities[index].generation);
}

EntityId player_entity(const World& world, PlayerId player) {
  if (player >= kMaxPlayers) return EntityId();
  return world.player_entities[player];
}

std::size_t count_alive(const World& world) {
  std::size_t count = 0;
  for (const Entity& entity : world.entities) {
    if (entity.alive) ++count;
  }
  return count;
}

bool entities_overlap(const Entity& a, const Entity& b) {
  const float ax = a.size.x * 0.5f;
  const float ay = a.size.y * 0.5f;
  const float bx = b.size.x * 0.5f;
  const float by = b.size.y * 0.5f;
  return a.position.x - ax < b.position.x + bx && b.position.x - bx < a.position.x + ax &&
         a.position.y - ay < b.position.y + by && b.position.y - by < a.position.y + ay &&
         a.position.z < b.position.z + b.size.z && b.position.z < a.position.z + a.size.z;
}

std::uint32_t next_random(World& world) {
  // xorshift64*: tiny, fast, and good enough for gameplay randomness.
  std::uint64_t x = world.rng_state;
  if (x == 0) x = 0x9E3779B97F4A7C15ull;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  world.rng_state = x;
  return static_cast<std::uint32_t>((x * 0x2545F4914F6CDD1Dull) >> 32);
}

float random_unit(World& world) {
  return static_cast<float>(next_random(world) >> 8) * (1.0f / 16777216.0f);
}

}  // namespace scraps
