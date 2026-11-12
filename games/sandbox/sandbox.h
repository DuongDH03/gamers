#pragma once

#include <cstddef>
#include <cstdint>

#include "core/command.h"
#include "core/game_api.h"
#include "core/simulate.h"
#include "core/world.h"

// The sandbox game: a small example of how a game sits on top of the engine.
//   - Walk around in any of the three views.
//   - Touch a coin (kind 2) to pick it up; your count is shown in the HUD.
//   - Press E to drop a coin (a game-defined command, sent over the network
//     like any other).
//   - Left-click a cell near you to place a coin there (the command carries
//     the clicked world point; the server checks the reach).
// Everything game-specific lives here; the engine knows nothing about coins.
namespace sandbox {

// Entity kinds, matching "kind N" in the scene files.
constexpr std::uint16_t kKindPlayer = 1;
constexpr std::uint16_t kKindCoin = 2;
constexpr std::uint16_t kKindWanderer = 3;

// Game commands.
constexpr scraps::CommandType kDropCoin = scraps::game_command(0);
// vec: the clicked world point. Places a coin standing in the grid cell that
// contains it, if that cell is empty, its bottom centre is within kPlaceReach
// of the player's position, and the player holds a coin.
constexpr scraps::CommandType kPlaceCoin = scraps::game_command(1);
constexpr float kPlaceReach = 3.0f;

// How the game uses Entity::user.
constexpr int kUserCoins = 0;         // on players: coins held
constexpr int kUserPickupDelay = 1;   // on coins: ticks until it can be picked up
constexpr std::int32_t kDropDelayTicks = 45;

// The game's simulate: the engine's simulate plus the coin rules. Pass this
// to Server and Client. `jobs` (optional) lets engine movement use threads.
void simulate(scraps::World& world, const scraps::CommandList& commands, scraps::Tick tick,
              scraps::JobSystem* jobs = nullptr);

std::int32_t coins_of(const scraps::World& world, scraps::PlayerId player);

// What the local player should hear for one tick, as sound names from the
// scene ("jump", "coin", "drop"), added to `out`. Presentation only: compares
// two worlds, a tick apart, and changes neither.
void sound_events(const scraps::World& before, const scraps::World& after, scraps::PlayerId player,
                  scraps::SoundEvents& out);

// The HUD text for `player`: "coins 3". Always terminated.
void hud(const scraps::World& world, scraps::PlayerId player, char* out, std::size_t size);

// The sandbox as a GameApi (core/game_api.h): what the apps use, whether the
// game is linked into the exe or loaded as a module (sandbox_api.cpp).
const scraps::GameApi* game_api();

}  // namespace sandbox
