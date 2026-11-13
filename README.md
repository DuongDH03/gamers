# Scraps

A small 2D game engine in C++17, written from scratch to learn how engines work.
One engine for **top-down, side-view and isometric** games, playable **offline or
online with 5 to 10 players** on a server the developer runs.

The rules every change must follow are in the
[engine constitution](.claude/skills/engine-constitution/SKILL.md). Read it first.

## Quick start

Requirements: CMake 3.25+ (the presets and workflows need it), and MinGW-w64
GCC or Visual Studio 2022. SDL3 is downloaded and built automatically the
first time (about 2 minutes).

```bash
cmake --preset mingw
```

```bash
cmake --build --preset mingw
```

```bash
ctest --preset mingw
```

Use `msvc` instead of `mingw` for Visual Studio, or `headless` to build
without SDL3 (no window; the server, the tests and `--headless` runs only).

Play offline (the server runs inside the client):

```bash
build/mingw/apps/scraps_client.exe --scene isometric
```

Bundled scenes: `top-down`, `side`, `isometric`, `crowd` (1,000 wanderers).

| Key | Action |
|---|---|
| WASD / arrows | move (in the side view, W / Up also jump) |
| Space | jump (side and isometric) |
| E | drop a coin (a game-defined command) |
| V | show the same scene through the next view |
| `-` / `=` | zoom out / in |
| Esc | quit |

Play online: one machine runs the server, everyone else connects.

```bash
build/mingw/apps/scraps_server.exe --scene top-down --port 7777
```

```bash
build/mingw/apps/scraps_client.exe --connect 192.168.1.20:7777
```

The client loads the scene the server names from its own `--scene-dir` and
checks it is byte-for-byte the same file (a hash).

**Hot reload:** while the game (offline) or the server runs, edit and save the
`.scene` file: the level is rebuilt and everyone re-joins it. Errors are shown
and the old level keeps running.

**Art:** the sandbox's pixel art is generated, not hand-made binary files.
Change `tools/make_sandbox_art.py` and run it:

```bash
python tools/make_sandbox_art.py
```

**Threads:** the simulation spreads movement over `cores − 1` worker threads
(`--threads N` to change, `0` for none). `scraps_race_demo --unsafe` shows why
threads need care: a shared counter loses most of its updates.

## How it fits together

```
 apps/client  ──picks──►  backends/sdl3 or backends/headless   (Window, Input, Renderer2D, Audio)
      │                   backends/udp or net/loopback          (Net)
      ▼
 games/sandbox   the game: coin rules, its own simulate()
      ▼
 views/          TopDownView · SideView · IsometricView         projection, draw order, movement rules
 net/            Server · Client · protocol                     server authority, prediction
      ▼
 core/           World · simulate · commands · scenes · loop    no idea which way is "down"
      ▼
 platform/       interfaces only
```

One frame of the offline client:

```
 pump events ─► sample keys ─► clock.advance(frame time) = N ticks
                                   │  for each tick:
                                   │    Controls → Commands ─► Client.tick (send + predict)
                                   │                          ─► Server.tick (the real simulate)
                                   │                          ─► Client.poll (snapshot → reconcile)
                                   ▼
                     View.draw(previous, current, alpha) ─► one triangle batch ─► present
```

Details: [docs/architecture.md](docs/architecture.md).

## Making a game

1. **Write a scene** (`.scene`, plain text): the grid, tiles, archetypes, spawns.
   Format: [docs/scene-format.md](docs/scene-format.md).
2. **Write your rules** as a `simulate` that calls the engine's and adds yours:

   ```cpp
   void my_simulate(World& world, const CommandList& commands, Tick tick) {
     scraps::simulate(world, commands, tick);   // joins, movement, collision
     for (const Command& c : commands)
       if (c.type == kMyCommand) { /* ... */ }  // your commands
     my_systems(world);                         // your systems
   }
   ```
3. **Pass it** to `Server` and `Client`. The same function runs on the server
   and in the client's prediction, so online and offline behave the same.

[games/sandbox](games/sandbox/sandbox.cpp) is a complete, small example.

## Repository

| Path | What |
|---|---|
| `engine/platform` | interfaces the core talks to (Rule 3) |
| `engine/core` | World, entities, commands, simulate, movement, scenes, serialization, fixed-step clock |
| `engine/views` | the three Views and keyboard → command mapping |
| `engine/net` | protocol, Server, Client, in-memory loopback network |
| `engine/backends` | `sdl3`, `headless`, `udp` |
| `apps` | `scraps_client`, `scraps_server`, `scraps_bench`, `scraps_race_demo` |
| `engine/third_party` | stb_image (pinned; PNG decoding) |
| `tools` | `make_sandbox_art.py` (generates the sandbox's PNGs) |
| `games/sandbox` | the example game and its scenes |
| `tests` | unit, integration and allocation tests (`ctest`) |
| `docs` | [plan](docs/plan.md), [architecture](docs/architecture.md), [decisions](docs/decisions), [specs](docs/specs), [Rust learning plan](docs/rust-plan.md) |
| `.claude` | the constitution, skills and agents for working on this repo |
