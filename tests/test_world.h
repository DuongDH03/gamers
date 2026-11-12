#pragma once

// Shared fixtures for tests: a small scene that every core test can use.

#include <cstdio>
#include <string>

#include "core/scene.h"
#include "core/simulate.h"
#include "core/world.h"

namespace test {

// 6 x 6 x 2 grid. Ground everywhere at z = 0 except a pit at (5, 5); one wall
// block at (3, 2, 1). Players spawn at (1.5, 2.5, 1). A gravity-affected rock
// sits over the pit.
inline const char* kBoxScene =
    "scraps-scene 1\n"
    "view top-down\n"
    "size 6 6 2\n"
    "tile g ground solid 10 20 30\n"
    "tile # wall solid 40 50 60\n"
    "archetype hero kind 1 size 0.6 0.6 0.9 color 200 0 0 speed 5 flags moves,gravity,collides\n"
    "archetype rock kind 9 size 0.5 0.5 0.5 color 90 90 90 flags moves,gravity,collides\n"
    "player hero\n"
    "spawn 1.5 2.5 1\n"
    "entity rock 5.5 5.5 1\n"
    "layer 0\n"
    "gggggg\n"
    "gggggg\n"
    "gggggg\n"
    "gggggg\n"
    "gggggg\n"
    "ggggg.\n"
    "layer 1\n"
    "......\n"
    "......\n"
    "...#..\n"
    "......\n"
    "......\n"
    "......\n";

inline scraps::MovementRules flat_rules() {
  scraps::MovementRules rules;
  return rules;
}

inline scraps::MovementRules gravity_rules() {
  scraps::MovementRules rules;
  rules.gravity = scraps::Vec3(0.0f, 0.0f, -30.0f);
  rules.jump_speed = 10.0f;
  return rules;
}

inline scraps::World make_box_world(const scraps::MovementRules& rules) {
  scraps::Scene scene;
  std::string error;
  if (!scraps::parse_scene(kBoxScene, scene, error)) {
    std::printf("fixture scene failed to parse: %s\n", error.c_str());
  }
  return scraps::make_world(scene, rules);
}

inline void step(scraps::World& world, const scraps::CommandList& commands) {
  scraps::simulate(world, commands, world.tick + 1);
}

inline void step(scraps::World& world) { step(world, scraps::CommandList()); }

inline scraps::CommandList one(const scraps::Command& command) {
  scraps::CommandList list;
  list.push(command);
  return list;
}

}  // namespace test
