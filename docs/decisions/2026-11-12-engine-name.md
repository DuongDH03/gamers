# Engine name: Scraps

- Date: 2026-11-12
- Status: **Accepted** (owner, 2026-11-12)

## Problem

The constitution left the engine's name open. Code needs it: the C++
namespace, the CMake project, target and executable names.

## Options

1. **Scraps**: the name of the owner's original project folder.
2. Gamers: the name of the current repository folder.
3. Dzo2D: the owner's initials.

## Decision

**Scraps.** Namespace `scraps`, backends in `scraps::sdl3`,
`scraps::headless`, `scraps::udp`. Targets are `scraps_*`; executables are
`scraps_client`, `scraps_server`, `scraps_bench`.
