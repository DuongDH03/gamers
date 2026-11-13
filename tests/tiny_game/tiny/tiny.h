#pragma once

#include "core/game_api.h"

// The tiny test game (tests/tiny_game/CMakeLists.txt): the engine's simulate,
// no game commands, no sounds, and a HUD that says which game this is.
namespace tiny {

const scraps::GameApi* game_api();

}  // namespace tiny
