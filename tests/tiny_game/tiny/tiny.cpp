// The tiny test game as a GameApi. Like the sandbox, only the module build
// (SCRAPS_GAME_MODULE) exports scraps_game_api().

#include "tiny/tiny.h"

#include <cstdio>

namespace tiny {
namespace {

void hud(const scraps::World& /*world*/, scraps::PlayerId /*player*/, char* out, std::size_t size) {
  if (out != nullptr && size > 0) std::snprintf(out, size, "tiny game");
}

void simulate(scraps::World& world, const scraps::CommandList& commands, scraps::Tick tick, scraps::JobSystem* jobs) {
  scraps::simulate(world, commands, tick, jobs);
}

constexpr scraps::GameApi kApi = {
    scraps::kGameApiVersion,
    scraps::kGameLayoutHash,
    "tiny",
    &tiny::simulate,
    nullptr,
    0,
    nullptr,
    &tiny::hud,
    nullptr,  // no UI commands
    0,
};

}  // namespace

const scraps::GameApi* game_api() { return &kApi; }

}  // namespace tiny

#if defined(SCRAPS_GAME_MODULE)
extern "C" SCRAPS_GAME_EXPORT const scraps::GameApi* scraps_game_api() { return tiny::game_api(); }
#endif
