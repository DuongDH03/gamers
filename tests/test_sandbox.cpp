#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>

#include "core/scene.h"
#include "harness.h"
#include "sandbox/sandbox.h"
#include "views/view_factory.h"

using namespace scraps;

namespace {

void step(World& world, const CommandList& commands) { sandbox::simulate(world, commands, world.tick + 1); }

void coins_are_picked_up_and_dropped(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, "top-down", scene, error));
  World world = make_world(scene, movement_rules_for(scene.level.perspective));

  CommandList join;
  join.push(make_command(CommandType::kPlayerJoin, 0));
  step(world, join);
  const Entity* player = find_entity(world, player_entity(world, 0));
  CHECK(player != nullptr);
  if (player == nullptr) return;
  CHECK(sandbox::coins_of(world, 0) == 0);

  // Test setup (not gameplay): put a coin right at the player's feet.
  const int coin_archetype = find_archetype(world.level, "coin");
  CHECK(coin_archetype >= 0);
  const EntityId coin = spawn_entity(world, static_cast<std::uint16_t>(coin_archetype), player->position);
  const std::size_t alive = count_alive(world);

  step(world, CommandList());
  CHECK(find_entity(world, coin) == nullptr);
  CHECK(sandbox::coins_of(world, 0) == 1);
  CHECK(count_alive(world) == alive - 1);

  // Drop it: a game command. It cannot be picked up again straight away...
  CommandList drop;
  drop.push(make_command(sandbox::kDropCoin, 0));
  step(world, drop);
  CHECK(sandbox::coins_of(world, 0) == 0);
  CHECK(count_alive(world) == alive);
  for (int i = 0; i < sandbox::kDropDelayTicks - 2; ++i) step(world, CommandList());
  CHECK(sandbox::coins_of(world, 0) == 0);

  // ...but after the delay it is.
  for (int i = 0; i < 4; ++i) step(world, CommandList());
  CHECK(sandbox::coins_of(world, 0) == 1);

  // Drop it again; then dropping with none left does nothing.
  step(world, drop);
  step(world, drop);
  CHECK(sandbox::coins_of(world, 0) == 0);
  step(world, drop);
  CHECK(sandbox::coins_of(world, 0) == 0);
}

// Coins standing exactly at `spot`.
int coins_at(const World& world, const Vec3& spot) {
  int n = 0;
  for (const Entity& entity : world.entities) {
    if (entity.alive && entity.kind == sandbox::kKindCoin && entity.position == spot) ++n;
  }
  return n;
}

// The click's point comes from a client; the game's simulate (on the server)
// decides whether it is allowed.
void place_coin_checks_reach(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, "top-down", scene, error));
  World world = make_world(scene, movement_rules_for(scene.level.perspective));
  CommandList join;
  join.push(make_command(CommandType::kPlayerJoin, 0));
  step(world, join);
  Entity* player = find_entity(world, player_entity(world, 0));
  CHECK(player != nullptr);
  if (player == nullptr) return;
  const Vec3 feet = player->position;  // (3.5, 2.5, 1): the top of the ground layer
  CHECK(feet == Vec3(3.5f, 2.5f, 1.0f));

  auto place = [](const Vec3& point) {
    CommandList list;
    list.push(make_command(sandbox::kPlaceCoin, 0, point));
    return list;
  };
  const std::size_t alive = count_alive(world);
  const Vec3 in_reach(5.8f, 2.1f, 1.0f);  // the cell (5, 2, 1), two cells to the right
  const Vec3 spot(5.5f, 2.5f, 1.0f);      // where its coin stands

  // No coins: nothing happens.
  step(world, place(in_reach));
  CHECK(count_alive(world) == alive);
  CHECK(coins_at(world, spot) == 0);

  // Test setup (not gameplay): the player holds two coins.
  find_entity(world, player_entity(world, 0))->user[sandbox::kUserCoins] = 2;

  // Refused: out of reach, inside the wall (row 0 is wall, 2 units away),
  // outside the grid, absurd or not a number. None of them costs a coin.
  const float nan = std::nanf("");
  for (const Vec3& bad : {Vec3(8.5f, 2.5f, 1.0f), Vec3(feet.x, 0.5f, 1.0f), Vec3(3.5f, -0.5f, 1.0f),
                          Vec3(3.5f, 2.5f, 1e30f), Vec3(-1e30f, 2.5f, 1.0f), Vec3(nan, 2.5f, 1.0f),
                          Vec3(4.5f, 2.5f, std::numeric_limits<float>::infinity())}) {
    step(world, place(bad));
    CHECK(count_alive(world) == alive);
    CHECK(sandbox::coins_of(world, 0) == 2);
  }

  // In reach, an empty cell: a coin stands at the bottom centre of that cell.
  step(world, place(in_reach));
  CHECK(count_alive(world) == alive + 1);
  CHECK(coins_at(world, spot) == 1);
  CHECK(sandbox::coins_of(world, 0) == 1);

  // Three clicks in one tick: each one is checked on its own, so the last
  // coin goes and the other two are refused.
  CommandList twice = place(Vec3(3.2f, 4.9f, 1.0f));
  twice.push(make_command(sandbox::kPlaceCoin, 0, Vec3(3.2f, 4.9f, 1.0f)));
  twice.push(make_command(sandbox::kPlaceCoin, 0, Vec3(3.2f, 4.9f, 1.0f)));
  step(world, twice);
  CHECK(sandbox::coins_of(world, 0) == 0);
  CHECK(count_alive(world) == alive + 2);
}

}  // namespace

void sound_events_follow_the_local_player(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, "side", scene, error));
  World before = make_world(scene, movement_rules_for(scene.level.perspective));
  CommandList join;
  join.push(make_command(CommandType::kPlayerJoin, 0));
  step(before, join);
  for (int i = 0; i < 30; ++i) step(before, CommandList());  // settle on the ground
  World after = before;
  Entity* me = find_entity(after, player_entity(after, 0));
  CHECK(me != nullptr && me->on_ground);
  if (me == nullptr) return;

  auto events_of = [](const World& a, const World& b, PlayerId player) {
    SoundEvents out;
    sandbox::sound_events(a, b, player, out);
    return out;
  };
  CHECK(events_of(before, after, 0).count == 0);  // nothing happened
  me->on_ground = false;
  me->velocity.z = 8.0f;
  me->user[sandbox::kUserCoins] = 1;
  const SoundEvents events = events_of(before, after, 0);
  CHECK(events.count == 2);
  CHECK(std::string(events.name(0)) == "jump" && std::string(events.name(1)) == "coin");
  World dropped = after;
  find_entity(dropped, player_entity(dropped, 0))->user[sandbox::kUserCoins] = 0;
  const SoundEvents drop = events_of(after, dropped, 0);
  CHECK(drop.count == 1 && std::string(drop.name(0)) == "drop");
  CHECK(events_of(before, after, 5).count == 0);  // another player: not ours to hear
  // Every name the game uses exists in the scene.
  for (const char* name : {"jump", "coin", "drop"}) CHECK(find_sound(scene.level, name) >= 0);
}

void register_sandbox_tests(test::Registry& registry) {
  registry.add("sandbox.sound_events_follow_the_local_player", sound_events_follow_the_local_player);
  registry.add("sandbox.coins_are_picked_up_and_dropped", coins_are_picked_up_and_dropped);
  registry.add("sandbox.place_coin_checks_reach", place_coin_checks_reach);
}
