#pragma once

#include <cstdint>

namespace scraps {

// The number of a simulation step. The world advances exactly one Tick per
// call to simulate (Rule 2).
using Tick = std::uint32_t;

constexpr int kTickRate = 60;
constexpr float kTickSeconds = 1.0f / static_cast<float>(kTickRate);

}  // namespace scraps
