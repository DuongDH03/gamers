---
name: build-and-test
description: How to configure, build, test, run and benchmark the Scraps engine in this repository. Use it whenever you need to compile the code, run the test suite, launch the client or server, check a change works, measure frame times, or diagnose a build or test failure. Use it before reporting any change as done.
---

# Build and test

## Commands (from the repository root)

| Step | MinGW GCC | Visual Studio 2022 | No SDL3 (headless) |
|---|---|---|---|
| Configure | `cmake --preset mingw` | `cmake --preset msvc` | `cmake --preset headless` |
| Build | `cmake --build --preset mingw` | `cmake --build --preset msvc` | `cmake --build --preset headless` |
| Test | `ctest --preset mingw` | `ctest --preset msvc` | `ctest --preset headless` |
| Rebuild only the game module (hot reload) | `cmake --build --preset mingw-game` | `cmake --build --preset msvc-game` | |

The first configure downloads and builds SDL3 (pinned tag and SHA-256 in
`cmake/ScrapsSDL3.cmake`); it takes a few minutes. Executables land in
`build/<preset>/apps/` (MSVC: `build/msvc/apps/Release/`).

A change is done only when **both** `mingw` and `msvc` build with zero warnings
(warnings are errors) and `ctest` passes on both. GCC and MSVC catch different
things: MSVC flags narrowing through `std::make_unique` arguments, for example.

## What the tests cover

`ctest` runs 23 tests (98 unit test functions inside the `unit_*` ones):

| Test | Covers |
|---|---|
| `layer_rules` | no `#include` points the wrong way across layers (Rule 3); `windows.h`/`dlfcn.h` only in `backends/dynlib` |
| `layer_rules_catch_mixed_case` | the layer check also catches `<Windows.h>` spelled in any case |
| `boundary_hash_tracks_sources` | the game-module layout hash changes when an engine header, `jobs.cpp` or `world.cpp` changes |
| `unit_core` | fixed step, bytes, entity handles, simulate, collision, gravity, facing, serialization, delta snapshots, job system, PNG decoding, sound decoding in every format, mixing, file watching |
| `unit_scene` | scene parsing (images, textures, sprites too), error messages, bundled scenes |
| `unit_views` | projections, `screen_to_world` (the inverse), input mapping, isometric draw order, textures and batching, sprite animation |
| `unit_platform` | headless pointer, buttons and wheel latch per frame |
| `unit_controls` | keys and clicks latched per frame, emitted once per tick (a click carries its world point) |
| `unit_game` | game modules: the DLL matches the static game, swaps keep the world and run the new code, mismatches are refused, shared heap, stable-file wait, live-copy and non-ASCII paths |
| `unit_net` | server/client over an in-memory network with latency, loss and reordering; delta snapshots, the challenge-cookie handshake, rate limits and floods, interpolation, hot reload |
| `unit_sandbox` | the example game's rules (coins, placing a coin with the mouse) |
| `unit_udp` | a real client and server over UDP on 127.0.0.1 |
| `no_allocation_in_hot_path` | threaded simulate + world copy + delta encode/decode + mixing + mouse sampling and picking + polling a game module + textured draw with shadows allocate nothing after warm-up |
| `race_demo_atomic_is_exact`, `race_demo_plain_loses_updates` | stage 8's race lesson; the plain one is *expected* to fail, and is skipped on one core |
| `client_offline_*`, `server_runs`, `bench_runs`, `client_offline_hot_game`, `server_hot_game` | each executable runs end to end, also with the game loaded as a DLL |

After changing `tools/make_sandbox_sounds.py` or `tools/make_audio_fixtures.py`,
run them too; both need ffmpeg (`D:/msys64/ucrt64/bin/ffmpeg.exe`, installed
with `pacman -S mingw-w64-ucrt-x86_64-ffmpeg`). The audio test files live in
`tests/data/audio/`.

After changing `tools/make_sandbox_art.py`, run it (`python tools/make_sandbox_art.py`)
before testing; `views.bundled_art_loads_...` checks every scene's images load.

Run one group with output: `build/mingw/tests/scraps_tests.exe net.` (any name prefix).

## Adding a test

Write a function `void name(test::Context& ctx)` in the right `tests/test_*.cpp`,
use `CHECK(...)` / `CHECK_NEAR(a, b, tolerance)`, and add it to that file's
`register_*_tests` with a `group.name` string. A new file also needs an entry in
`tests/CMakeLists.txt` and a `register_*` call in `tests/test_main.cpp`.
`tests/test_world.h` has a small fixture scene and helpers.

## Running

```bash
build/mingw/apps/scraps_client.exe --scene isometric
```

- `--scene top-down|side|isometric|crowd`, `--view <view>`, `--connect ip:port`
- `--headless --frames N`: no window, scripted input, prints a summary and
  exits non-zero if the player never appeared. Offline headless runs use
  simulated time, so they are fast and repeatable.
- Server: `build/mingw/apps/scraps_server.exe --scene top-down --port 7777`
  (`--bind 127.0.0.1` keeps it local and avoids a firewall prompt; `--ticks N` stops it).
- `--hot-game` (client offline, or server): load the game as a DLL and swap in
  each new build. Edit `games/sandbox/*.cpp`, run `cmake --build --preset mingw-game`
  (or `msvc-game`), and the running program prints "game code reloaded".
  Don't build the full preset while the exe runs: Windows locks the running exe.
  Editing an `engine/core` or `engine/platform` header, `core/jobs.cpp` or
  `core/world.cpp` makes the module refuse to load until you restart.

## Benchmark

```bash
build/mingw/apps/scraps_bench.exe --scene crowd --ticks 600
```

Prints average and worst time for simulate, world copy, snapshot
serialization and each view's draw, against the 16.667 ms frame budget.
Compare with the reference numbers in `docs/architecture.md` section 7.
Always benchmark a Release build.

## When something fails

- MinGW executables exit with `0xc0000139` (entry point not found): another
  `libstdc++-6.dll` (for example Git's `mingw64`) comes first on PATH. Put the
  compiler's own `bin` first (here `D:/msys64/ucrt64/bin`) before running
  `ctest` or the executables.
- Configure cannot download SDL3: build with the `headless` preset, or install
  SDL3 and let `find_package` find it.
- `unit_udp` fails: something else may block UDP on 127.0.0.1 (security software).
- A smoke test fails: run the same command line by hand; the client prints why
  (`client failed: cannot load the server's scene ...`).
- A flaky test is a bug. The engine is deterministic given the same inputs;
  tests use simulated time and seeded randomness.
