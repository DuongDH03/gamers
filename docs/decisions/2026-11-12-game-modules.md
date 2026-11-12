# Games as modules, and hot-reloadable game code

- Date: 2026-11-12
- Status: **Accepted** (owner approved the spec, 2026-11-12).
- Spec and results: `docs/specs/2026-11-12-mouse-and-hot-reload.md`, Part B.

## Problem

Two needs, one seam:

1. The apps named the sandbox directly (`&sandbox::simulate`,
   `sandbox::coins_of`, `sandbox::sound_events`, key bindings). A second game
   would have meant editing `apps/client` and `apps/server`.
2. Changing game C++ meant rebuilding and restarting, which loses the world
   and your place in it. Unity's "Play mode" loop has no equivalent.

## Options

1. **The game as a DLL** (chosen). Every game describes itself with one
   plain struct, `GameApi` (`engine/core/game_api.h`), and exports
   `extern "C" const GameApi* scraps_game_api()`. Apps either link the game
   statically or, with `--hot-game`, load it as a module and swap in a new
   build between frames.
2. **The engine as DLLs too.** One copy of everything, and engine changes
   could reload as well, but every engine type would need export macros, and
   the whole build would change. Not needed for iterating on a game.
3. **Fast restart with saved state.** No DLLs, and it survives engine changes,
   but it is still a full rebuild plus a new window each time. A possible
   later addition.

## Decision

Option 1. What makes it safe:

- **The exe owns all state.** `World` is plain data with no pointers or
  callbacks (Rule 5's consequence), so nothing in it points into the module.
  Strings the module returns (name, sound names, HUD text) are copied.
- **Swaps happen between frames only**, on the main thread, with no tick or
  job running. Server and Client get the new `simulate` before the old
  library is unloaded.
- **Incompatible modules are refused.** The API version and a layout hash must
  match; on a mismatch the old code keeps running and the HUD says why.
- **One heap.** MSVC must use the `/MD` runtime (checked at compile time);
  MinGW uses UCRT `malloc` on both sides.
- **DLL loading is a backend** (`engine/backends/dynlib`), so `windows.h` and
  `dlfcn.h` stay out of core and apps (`layer_rules` checks it).
- **Release builds link games statically**, through the same `GameApi`.

## Limits

- Engine code in the exe (views, net, backends, apps) still needs a restart.
- So does editing any `engine/core` or `engine/platform` header,
  `core/jobs.cpp` or `core/world.cpp`, even only a comment: they are hashed
  into the layout check (`cmake/ScrapsBoundaryHash.cmake`), and a running
  program refuses a module built from changed ones.
- MSVC builds must use the `/MD` runtime, a static release too (the check in
  `game_api.h` does not yet distinguish builds that never load a module).
- Online, only the dedicated server reloads; a client connected to it keeps
  its own code, and reconciliation corrects its predictions.
- Changing what existing data means (for example `user[2]`) is the game
  developer's business; saving the `.scene` starts the world fresh.

## Consequences

- The scripting question (none / Lua / C++ only) gets a working answer for
  now, "C++ only, but reloadable", without closing it.
- A second game is a new folder under `games/` that exports a `GameApi`.
- New backend `dynlib` beside `sdl3`, `headless` and `udp` (constitution
  sections 3 and 4 list it).
