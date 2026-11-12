// A game module whose code visibly differs from the sandbox's
// (game.swap_runs_the_new_code): the sandbox rules, then every player entity
// gets the tick number in user[kMarkerSlot]. The sandbox never writes that
// slot, so a world that carries the marker was simulated by this code, and a
// world that does not was not. Built with the same engine headers as the
// tests, so its version and layout hash match and it is accepted.

#include "core/game_api.h"
#include "marker_game.h"
#include "sandbox/sandbox.h"

namespace {

void simulate_marked(scraps::World& world, const scraps::CommandList& commands, scraps::Tick tick,
                     scraps::JobSystem* jobs) {
  sandbox::simulate(world, commands, tick, jobs);
  for (scraps::Entity& entity : world.entities) {
    if (entity.alive && entity.player != scraps::kNoPlayer) {
      entity.user[marker_game::kMarkerSlot] = static_cast<std::int32_t>(tick);
    }
  }
}

constexpr scraps::GameBinding kBindings[] = {
    scraps::key_binding(scraps::Key::kE, sandbox::kDropCoin),
    scraps::button_binding(scraps::MouseButton::kLeft, sandbox::kPlaceCoin),
};

constexpr scraps::GameApi kApi = {
    scraps::kGameApiVersion,
    scraps::kGameLayoutHash,
    marker_game::kName,
    &simulate_marked,
    kBindings,
    sizeof(kBindings) / sizeof(kBindings[0]),
    &sandbox::sound_events,
    &sandbox::hud,
};

}  // namespace

extern "C" SCRAPS_GAME_EXPORT const scraps::GameApi* scraps_game_api() { return &kApi; }
