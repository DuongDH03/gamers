#include "sandbox/sandbox.h"

#include <cmath>
#include <cstdio>

namespace sandbox {
namespace {

using scraps::Command;
using scraps::CommandList;
using scraps::Entity;
using scraps::EntityId;
using scraps::World;

void drop_coin(World& world, scraps::PlayerId player) {
  Entity* owner = scraps::find_entity(world, scraps::player_entity(world, player));
  if (owner == nullptr || owner->user[kUserCoins] <= 0) return;
  const int coin_archetype = scraps::find_archetype(world.level, "coin");
  if (coin_archetype < 0) return;

  const scraps::Vec3 position = owner->position;
  owner->user[kUserCoins] -= 1;
  // Spawning may move the entity array, so `owner` is not used after this.
  Entity* coin = scraps::find_entity(world, scraps::spawn_entity(world, static_cast<std::uint16_t>(coin_archetype), position));
  if (coin != nullptr) coin->user[kUserPickupDelay] = kDropDelayTicks;
}

// The point comes from a client and is not trusted (Rule 5): it may be far
// away, inside a wall, outside the grid, or not a number at all.
void place_coin(World& world, scraps::PlayerId player, const scraps::Vec3& target) {
  if (!scraps::is_finite(target)) return;
  Entity* owner = scraps::find_entity(world, scraps::player_entity(world, player));
  if (owner == nullptr || owner->user[kUserCoins] <= 0) return;
  const int coin_archetype = scraps::find_archetype(world.level, "coin");
  if (coin_archetype < 0) return;

  // Far-away points are refused before they are turned into cell indices.
  const scraps::Vec3 offset = target - owner->position;
  if (std::fabs(offset.x) > 2.0f * kPlaceReach || std::fabs(offset.y) > 2.0f * kPlaceReach ||
      std::fabs(offset.z) > 2.0f * kPlaceReach) {
    return;
  }
  // The cell that holds the point (feet on a block top: the open cell above
  // it). The same rule draws the client's hover outline.
  scraps::CellCoord cell;
  if (!scraps::cell_at_point(target, cell)) return;
  const int cx = cell.x;
  const int cy = cell.y;
  const int cz = cell.z;
  const scraps::TileGrid& grid = world.level.grid;
  // Standing on the top layer, the cell above it (just outside the grid) is fine.
  if (cx < 0 || cy < 0 || cz < 0 || cx >= grid.width || cy >= grid.depth || cz > grid.height) return;
  if (scraps::is_solid_cell(world.level, cx, cy, cz)) return;

  const scraps::Vec3 spot(static_cast<float>(cx) + 0.5f, static_cast<float>(cy) + 0.5f, static_cast<float>(cz));
  const scraps::Vec3 reach = spot - owner->position;
  if (reach.x * reach.x + reach.y * reach.y + reach.z * reach.z > kPlaceReach * kPlaceReach) return;

  owner->user[kUserCoins] -= 1;
  // Spawning may move the entity array, so `owner` is not used after this.
  Entity* coin = scraps::find_entity(world, scraps::spawn_entity(world, static_cast<std::uint16_t>(coin_archetype), spot));
  if (coin != nullptr) coin->user[kUserPickupDelay] = kDropDelayTicks;
}

void collect_coins(World& world) {
  for (std::uint32_t i = 0; i < world.entities.size(); ++i) {
    Entity& coin = world.entities[i];
    if (!coin.alive || coin.kind != kKindCoin) continue;
    if (coin.user[kUserPickupDelay] > 0) {
      coin.user[kUserPickupDelay] -= 1;
      continue;
    }
    for (const EntityId& id : world.player_entities) {
      Entity* player = scraps::find_entity(world, id);
      if (player == nullptr || !scraps::entities_overlap(*player, coin)) continue;
      player->user[kUserCoins] += 1;
      // Destroying frees the slot but never moves other entities, so the
      // index loop stays valid.
      scraps::destroy_entity(world, scraps::entity_id_at(world, i));
      break;
    }
  }
}

}  // namespace

void simulate(World& world, const CommandList& commands, scraps::Tick tick, scraps::JobSystem* jobs) {
  // 1. Engine rules: joins, leaves, movement, collision.
  scraps::simulate(world, commands, tick, jobs);

  // 2. Game commands.
  for (const Command& command : commands) {
    if (command.type == kDropCoin) drop_coin(world, command.player);
    if (command.type == kPlaceCoin) place_coin(world, command.player, command.vec);
  }

  // 3. Game systems.
  collect_coins(world);
}

std::int32_t coins_of(const World& world, scraps::PlayerId player) {
  const Entity* entity = scraps::find_entity(world, scraps::player_entity(world, player));
  return entity != nullptr ? entity->user[kUserCoins] : 0;
}

void sound_events(const scraps::World& before, const scraps::World& after, scraps::PlayerId player,
                  scraps::SoundEvents& out) {
  const scraps::Entity* was = scraps::find_entity(before, scraps::player_entity(before, player));
  const scraps::Entity* now = scraps::find_entity(after, scraps::player_entity(after, player));
  if (was == nullptr || now == nullptr) return;
  if (was->on_ground && !now->on_ground && now->velocity.z > 0.0f) out.add("jump");
  const std::int32_t coins_before = was->user[kUserCoins];
  const std::int32_t coins_after = now->user[kUserCoins];
  if (coins_after > coins_before) out.add("coin");
  if (coins_after < coins_before) out.add("drop");
}

void hud(const World& world, scraps::PlayerId player, char* out, std::size_t size) {
  if (out == nullptr || size == 0) return;
  std::snprintf(out, size, "coins %d", static_cast<int>(coins_of(world, player)));
}

}  // namespace sandbox
