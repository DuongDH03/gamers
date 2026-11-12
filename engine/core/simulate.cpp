#include "core/simulate.h"

#include "core/movement.h"

namespace scraps {
namespace {

void join_player(World& world, PlayerId player) {
  if (find_entity(world, player_entity(world, player)) != nullptr) return;  // already in the game
  const EntityId id =
      spawn_entity(world, world.level.player_archetype, spawn_point_for(world.level, player));
  Entity* entity = find_entity(world, id);
  if (entity == nullptr) return;
  entity->player = player;
  world.player_entities[player] = id;
}

void leave_player(World& world, PlayerId player) {
  destroy_entity(world, player_entity(world, player));
  world.player_entities[player] = EntityId();
}

void set_move_intent(World& world, PlayerId player, const Vec3& direction) {
  Entity* entity = find_entity(world, player_entity(world, player));
  if (entity == nullptr) return;
  Vec2 intent(direction.x, direction.y);
  if (!is_finite(intent)) intent = Vec2();
  if (!world.rules.move_x) intent.x = 0.0f;
  if (!world.rules.move_y) intent.y = 0.0f;
  const float len = length(intent);
  if (len > 1.0f) intent = intent * (1.0f / len);
  entity->move_intent = intent;
}

void jump(World& world, PlayerId player) {
  Entity* entity = find_entity(world, player_entity(world, player));
  if (entity == nullptr) return;
  if (world.rules.jump_speed <= 0.0f || !entity->on_ground) return;
  if ((entity->flags & kEntityGravity) == 0) return;
  entity->velocity.z = world.rules.jump_speed;
  entity->on_ground = false;
}

}  // namespace

void apply_commands(World& world, const CommandList& commands) {
  for (const Command& command : commands) {
    if (command.player >= kMaxPlayers) continue;
    switch (command.type) {
      case CommandType::kPlayerJoin: join_player(world, command.player); break;
      case CommandType::kPlayerLeave: leave_player(world, command.player); break;
      case CommandType::kMove: set_move_intent(world, command.player, command.vec); break;
      case CommandType::kJump: jump(world, command.player); break;
      default: break;  // kNone and game commands: handled by the game's simulate
    }
  }
}

void simulate(World& world, const CommandList& commands, Tick tick, JobSystem* jobs) {
  apply_commands(world, commands);
  step_movement(world, kTickSeconds, jobs);
  world.tick = tick;
}

}  // namespace scraps
