// A game module that must be refused (game.mismatch_is_refused): built from
// this one file three times, once claiming a newer game API version
// (SCRAPS_BAD_VERSION), once the version before this one, the one before UI
// commands (SCRAPS_BAD_OLD), and once another layout hash
// (SCRAPS_BAD_LAYOUT), as a module built against another engine would.

#include "core/game_api.h"

namespace {

void simulate_nothing(scraps::World&, const scraps::CommandList&, scraps::Tick, scraps::JobSystem*) {}

constexpr scraps::GameApi kApi = {
#if defined(SCRAPS_BAD_VERSION)
    scraps::kGameApiVersion + 1u,
    scraps::kGameLayoutHash,
#elif defined(SCRAPS_BAD_OLD)
    scraps::kGameApiVersion - 1u,
    scraps::kGameLayoutHash,
#else
    scraps::kGameApiVersion,
    scraps::kGameLayoutHash ^ 0x5A5Au,
#endif
    "bad",
    &simulate_nothing,
    nullptr,
    0,
    nullptr,
    nullptr,
    nullptr,
    0,
};

}  // namespace

extern "C" SCRAPS_GAME_EXPORT const scraps::GameApi* scraps_game_api() { return &kApi; }
