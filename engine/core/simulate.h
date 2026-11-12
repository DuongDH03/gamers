#pragma once

#include "core/command.h"
#include "core/tick.h"
#include "core/world.h"

namespace scraps {

class JobSystem;

// The signature of a simulation step. The server and the client's prediction
// both call one of these, so a game plugs in its rules by passing its own.
// `jobs` may be nullptr; it only says how to run (on which threads), never
// what changes (docs/decisions/2026-11-12-simulate-jobs.md).
using SimulateFn = void (*)(World& world, const CommandList& commands, Tick tick, JobSystem* jobs);

// Rule 1: the one place a running World changes. Applies the commands, moves
// everything by one fixed step, and sets world.tick = tick. With a JobSystem,
// movement runs on worker threads; the result is identical either way.
//
// Games that need more rules write their own simulate that calls this one
// (or the building blocks below) and then runs game systems. See
// games/sandbox/sandbox.cpp.
void simulate(World& world, const CommandList& commands, Tick tick, JobSystem* jobs = nullptr);

// Building blocks of simulate(), for games that compose their own.
void apply_commands(World& world, const CommandList& commands);  // engine command types only

}  // namespace scraps
