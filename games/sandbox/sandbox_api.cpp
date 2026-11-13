// The sandbox described as a GameApi (core/game_api.h).
//
// This file is compiled into both builds of the game (games/CMakeLists.txt):
//   - scraps_sandbox (static): the apps call sandbox::game_api() directly;
//   - scraps_sandbox_module (shared, SCRAPS_GAME_MODULE defined): the C entry
//     point scraps_game_api() is exported too, for hot reload.
// Only the module defines scraps_game_api(), so linking the static game into
// a program never clashes with the module's export.

#include "sandbox/sandbox.h"

namespace sandbox {
namespace {

constexpr scraps::GameBinding kBindings[] = {
    scraps::key_binding(scraps::Key::kE, kDropCoin),
    scraps::button_binding(scraps::MouseButton::kLeft, kPlaceCoin),
    scraps::pad_binding(scraps::GamepadButton::kWest, kDropCoin),
};

// What a UI may ask for by name: a "drop coin" button on the HUD.
constexpr scraps::GameUiCommand kUiCommands[] = {
    {"drop_coin", kDropCoin},
};

// The function pointers below point into whichever build this is; in the
// module they die with it, which is why the apps copy the name, the bindings
// and the UI command names.
constexpr scraps::GameApi kApi = {
    scraps::kGameApiVersion,
    scraps::kGameLayoutHash,
    "sandbox",
    &sandbox::simulate,
    kBindings,
    sizeof(kBindings) / sizeof(kBindings[0]),
    &sandbox::sound_events,
    &sandbox::hud,
    kUiCommands,
    sizeof(kUiCommands) / sizeof(kUiCommands[0]),
};

}  // namespace

const scraps::GameApi* game_api() { return &kApi; }

}  // namespace sandbox

#if defined(SCRAPS_GAME_MODULE)
extern "C" SCRAPS_GAME_EXPORT const scraps::GameApi* scraps_game_api() { return sandbox::game_api(); }
#endif
