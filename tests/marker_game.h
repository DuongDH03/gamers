#pragma once

// What the marker test module (marker_game_module.cpp) does differently from
// the sandbox, shared with the test that checks for it (test_game.cpp).
namespace marker_game {

constexpr const char* kName = "sandbox-marked";
// The Entity::user slot the module stamps with the tick on every player.
// The sandbox uses slots 0 and 1 only.
constexpr int kMarkerSlot = 3;

}  // namespace marker_game
