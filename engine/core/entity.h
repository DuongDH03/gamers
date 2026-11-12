#pragma once

#include <array>
#include <cstdint>

#include "core/math.h"
#include "platform/color.h"

namespace scraps {

using PlayerId = std::uint8_t;
constexpr PlayerId kNoPlayer = 0xFF;
constexpr int kMaxPlayers = 16;

// Hard upper bound on entity slots; also used to reject malformed snapshots.
constexpr std::uint32_t kMaxEntities = 65536;

// A handle to an entity: a slot index plus the slot's generation. When an
// entity is destroyed its slot's generation goes up, so old handles to that
// slot stop resolving instead of silently pointing at a new entity.
// Entities refer to each other only through these, never through pointers.
struct EntityId {
  static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

  std::uint32_t index = kInvalidIndex;
  std::uint32_t generation = 0;

  constexpr EntityId() = default;
  constexpr EntityId(std::uint32_t index_value, std::uint32_t generation_value)
      : index(index_value), generation(generation_value) {}

  constexpr bool is_valid() const { return index != kInvalidIndex; }
};

inline bool operator==(EntityId a, EntityId b) { return a.index == b.index && a.generation == b.generation; }
inline bool operator!=(EntityId a, EntityId b) { return !(a == b); }

// Behaviour flags. Movement is applied by core/movement.cpp.
constexpr std::uint32_t kEntityMoves = 1u << 0;     // integrates velocity every tick
constexpr std::uint32_t kEntityGravity = 1u << 1;   // affected by MovementRules::gravity
constexpr std::uint32_t kEntityCollides = 1u << 2;  // blocked by solid tiles
constexpr std::uint32_t kEntityBounces = 1u << 3;   // reflects velocity instead of stopping

// One entity. The entity model is "array of structs" (docs/decisions): every
// entity has every component field, and `flags` says which behaviours apply.
// Plain data only: no pointers, no callbacks (Rule 5).
struct Entity {
  // Slot bookkeeping.
  std::uint32_t generation = 1;
  bool alive = false;

  // What it is. `archetype` indexes Level::archetypes; `kind` is a game-defined tag.
  std::uint16_t archetype = 0;
  std::uint16_t kind = 0;
  std::uint32_t flags = 0;

  // Where it is. `position` is the bottom-center of its box, in world units.
  Vec3 position;
  Vec3 velocity;
  Vec3 size{0.8f, 0.8f, 0.8f};

  // Who controls it, and what they asked for.
  PlayerId player = kNoPlayer;
  Vec2 move_intent;  // world ground direction (x, y), length <= 1
  // The way it last moved, on the ground plane (x, y), length 1. Kept when it
  // stops, so a character still faces where it was going. Set by simulate.
  Vec2 facing{1.0f, 0.0f};
  float move_speed = 0.0f;
  bool on_ground = false;

  // How it looks.
  Color color{200, 200, 200};

  // Game-defined state (score, timers, ...). The engine never reads these.
  std::array<std::int32_t, 4> user{};
};

}  // namespace scraps
