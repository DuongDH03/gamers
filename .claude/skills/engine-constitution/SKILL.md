---
name: engine-constitution
description: The design rules of this 2D C++ game engine (Scraps). Read this before touching any code, spec, plan, or review in this repository, even for a one-line change, because five decisions here (command boundary, fixed timestep, platform layer, projection-agnostic core, dev-hosted server) decide what is allowed. Also use it when the owner asks "where does X go", "how should Y be structured", "can we just call Z directly", or questions any architectural decision. If a change would break a rule below, stop and raise it with the owner instead of working around it.
---

# Engine constitution

This is the agreement between the owner (DZO) and everyone, human or agent, who works on this engine.
It records decisions already made so they do not get re-decided by accident. It is short on purpose.
When something is not covered here, it is an open decision (see the end), not a free choice.

## 1. What we are building

A 2D game engine, in C++17, for the owner's own games. Those games may be top-down, side-view, or
isometric; single-player or online with 5 to 10 players. The owner's goal is to understand how engines
work, so the core is written by us, and libraries are used only at the edges (windowing, GPU, sockets).

Non-goals: 3D, more than ~16 players, a general-purpose editor on day one, matching Unreal or Unity.

## 2. The five rules

These are the load-bearing decisions. Each has a "why", because the rule only holds if people
understand what it protects.

### Rule 1. Nothing changes the world except `simulate`

```cpp
void simulate(World& world, const CommandList& commands, Tick tick, JobSystem* jobs = nullptr);
```

`jobs` only says *how* to run (on which worker threads, stage 8), never *what* changes; the result
is identical with or without it, and the three-argument call still works.

Input, UI, scripts, and the network never modify `World` directly. They produce `Command`s
(`Move`, `Attack`, `UseItem`), and `simulate` is the only function that reads commands and mutates state.

Why: this one boundary is what makes online and offline the same code. Offline, commands come from the
local keyboard. Online, they also arrive over a socket. If gameplay code reaches into the world
directly, multiplayer becomes a rewrite. It also makes replays, rollback, and tests nearly free.

Cost we accept: a UI button that heals the player cannot call `player.heal()`. It issues a command.
That is tedious and it is the price.

### Rule 2. Fixed timestep simulation, free-running render

`simulate` always runs with the same step (default 60 Hz). The render loop runs as fast as the
platform allows and draws the world using an interpolation alpha between the last two ticks.

Why: fixed steps make physics stable and make the game behave identically on every machine, which the
server model (Rule 5) requires. Decoupling render from simulation is also what later lets them live on
different threads.

### Rule 3. The core never includes a backend header

The core talks to platform interfaces only: `Window`, `Input`, `Renderer2D`, `Audio`, `Net`.
Backends implement them. Two backends exist from the beginning:

- `sdl3`: the real one, for players.
- `headless`: no window, no drawing, no audio. Runs the full simulation. Used by the dedicated
  server and by automated tests.

Why: the owner wants the engine to outlive any one library, and an abstraction with a single backend
quietly absorbs that backend's assumptions. The headless backend keeps the interfaces honest, and we
need it anyway for the server. A third backend (raylib, a browser build) is allowed later and must not
require core changes.

### Rule 4. The core does not know which way is "down"

`World` stores positions in world units as `(x, y, z)` where `z` is height above the ground plane.
A `View` (`TopDownView`, `SideView`, `IsometricView`) owns three things:

1. world-to-screen projection and the camera,
2. draw ordering (layers, y-sort, isometric depth),
3. movement rules (gravity or none, ground detection, allowed directions).

Core code, components, and `simulate` helpers must not assume a gravity direction, a sort order, or
that `z` is unused.

Why: the owner wants one engine for all three perspectives. Engines that start top-down and add
isometric later end up with three code paths. Isometric is the view that stresses this rule most, so it
gets designed first, not last.

### Rule 5. The server is the authority, and the developer runs it

The authoritative `simulate` runs in the `server` executable. The `client` executable renders, collects
input, sends commands, and predicts locally. Offline play is the client with the server linked in and
running in the same process. There is no separate "single-player mode" in the code.

Why: the owner does not want a game to depend on a player hosting it. Server-authoritative also means
we do not need bit-exact determinism across machines, so floating point in gameplay is fine.

Consequences for everyday code:
- `World` is plain, copyable data. No pointers between entities; use integer handles.
- No `std::function`, callbacks, or owning pointers inside `World`.
- Anything in `World` must be serializable to bytes without special cases.

## 3. Layers and the dependency direction

```
games/           the actual games: content, game-specific commands and systems
views/   net/    views: TopDownView, SideView, IsometricView, Controls
                 net: protocol, Server, Client, loopback network (sits beside views; neither includes the other)
core/            loop, World, simulate, commands, entities, scenes, serialization
platform/        interfaces: Window, Input, Renderer2D, Audio, Net
backends/        sdl3/, headless/, udp/   (implement platform interfaces)
                 webview2/                (WebView2 window: Window, Input, Renderer2D, UiHost)
                 dynlib/                  (loads game modules; used by apps only)
apps/            client/, web/, server/, bench/  (thin executables that wire the layers together)
```

Dependencies point downward only. `core` includes `platform` interfaces, never `backends`. `views`
and `net` include `core`. `games` include everything above `platform` except backends. `apps` are the
only place where a concrete backend is named. The `layer_rules` test (`cmake/CheckLayers.cmake`)
enforces this on every `ctest` run.

## 4. Repository layout

```
engine/
  core/  platform/  views/  net/
  backends/  sdl3/  headless/  udp/  dynlib/  webview2/
apps/
  client/  web/  server/  bench/
games/
  <one folder per game>   (sandbox/ is the example game)
tests/
docs/
  decisions/        one short file per decision, dated (see section 8)
  specs/            feature specs written before implementation
  plan.md           stage status and what comes next
  architecture.md   how the pieces fit, with diagrams
.claude/
  skills/  agents/
```

## 5. Code rules

Short list, each with a reason.

- C++17, warnings as errors (`-Wall -Wextra -Werror`, `/W4 /WX`). Warnings hide real bugs in engines.
- No exceptions in `core` and below. Errors return values. Exceptions and real-time loops mix badly.
- No singletons or global mutable state. Pass things explicitly. Globals are what make threading
  (stage 8) impossible to reason about.
- No raw `new`/`delete` outside allocators. Use value types, `std::vector`, `std::unique_ptr`.
- No allocation inside `simulate` or the render path once stage 5 is done. Allocation is a hidden
  frame-time spike. (Checked by the `no_allocation_in_hot_path` test.)
- Entities refer to each other by `EntityId` (integer handle), never by pointer or reference.
- Naming: `PascalCase` types, `snake_case` functions and variables, `kConstant` for constants,
  `m_member` for private members. Pick one and stay consistent; this is it.
- Every system that touches time takes a `Tick` or a `double dt` explicitly. Nothing reads the clock
  except the loop.

## 6. Build order

Something must run after every stage. Each stage has a "done" condition. Current status: `docs/plan.md`.

| Stage | What | Done when |
|---|---|---|
| 1 | Game loop | Fixed-step updates stay at 60/s while render fps changes |
| 2 | Platform layer + sdl3 + headless | Same demo runs with either backend, chosen in `apps/` |
| 3 | Input as commands | Keyboard produces `Command`s; `simulate` is the only mutator |
| 4 | Entities and components | 1,000 sprites on screen, `World` is copyable and serializable |
| 5 | Views | One scene shown top-down, side-view, and isometric by swapping the `View` |
| 6 | Collision and movement rules | A playable toy in each view |
| 7 | Assets and scenes | Levels load from files; hot reload |
| 8 | Job system | Physics on worker threads; a race-condition test that fails without locks |
| 9 | Netcode | `server` + two `client`s share a world; prediction and interpolation |

Order inside a stage: spec in `docs/specs/`, owner approves, implement, review, bench.

## 7. Decisions

Decided (each has a dated file in `docs/decisions/`):

- Engine name: **Scraps** (`2026-11-12-engine-name.md`).
- Entity model: **array of structs** with generational handles (`2026-11-12-entity-model.md`).
- Tile map: **one 3D block grid** for all views, **2:1 diamond** isometric, text `.scene` files
  (`2026-11-12-level-model.md`).
- Test framework: **in-house harness + CTest** (`2026-11-12-test-framework.md`).
- `Net` implementations: **`udp` backend and in-memory loopback** (`2026-11-12-net-backends.md`).
- Images: **PNG decoded by stb_image** (vendored, pinned); animation: **sprite sheets**
  (`2026-11-12-images-and-sprites.md`).
- Audio: **every common format** (WAV, AIFF, FLAC, MP3, OGG Vorbis) recognised by content and
  decoded in core by vendored single-file decoders; the engine mixes, the backend only plays
  (`2026-11-12-audio-formats.md`).
- `simulate` takes an optional **`JobSystem*`** fourth parameter for stage 8's worker threads
  (`2026-11-12-simulate-jobs.md`).
- Games describe themselves to the apps with a plain **`GameApi`** struct and can be loaded as a
  **hot-reloadable DLL** (`--hot-game`); release builds link them statically
  (`2026-11-12-game-modules.md`).
- UI: **layout A**, React/HTML in a WebView2 window that also draws the world with WebGL2 from the
  Views' triangle batches (`2026-11-14-ui-layout.md`). UI game actions are `Command`s.

Still open. Nobody picks these implicitly by writing code:

- Rust in the project: learning only, a UI shell, tools, or a port (`docs/rust-plan.md`, part 5).
- Scripting: none, Lua, or C++ only.
- The tool used to author levels.

## 8. Changing this document

Only the owner changes the five rules. Anyone may propose a change by writing
`docs/decisions/YYYY-MM-DD-<topic>.md` with the problem, the options, and a recommendation. If a rule
blocks a task, raise it; do not route around it.

History: 2026-11-12, sections 3, 4 and 7 updated to match the first implementation (net beside views,
`udp` backend, `bench` app, decisions recorded); in section 5 the threading stage is corrected to 8
(the job system), and the allocation rule names the test that checks it. Rules 1 to 5 are unchanged.
Later on 2026-11-12, section 7 recorded the images decision and the open Rust question. Then the
owner accepted `2026-11-12-simulate-jobs.md`, and Rule 1's example signature gained the optional
`JobSystem* jobs = nullptr` parameter. The rule itself (only `simulate` changes the World) is unchanged.
Later on 2026-11-12, section 7 recorded the audio decision (every common format) and dropped it
from the open list. Rules 1 to 5 are unchanged.
Later on 2026-11-12, sections 3, 4 and 7 recorded the `dynlib` backend and the game-module
decision (owner-approved spec `2026-11-12-mouse-and-hot-reload.md`). Rules 1 to 5 are unchanged.
On 2026-11-14, sections 3, 4 and 7 recorded the `webview2` backend, the `web` app and the UI layout
decision (owner-approved spec `2026-11-14-offline-essentials-and-ui.md`). Rules 1 to 5 are unchanged.
