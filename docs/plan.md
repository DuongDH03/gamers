# Build plan and status

The 9 stages come from the constitution (section 6). This file records where
each one stands. Last updated 2026-11-12.

| Stage | What | Status | Evidence |
|---|---|---|---|
| 1 | Game loop | **Done** | `FixedStepClock`; test `core.fixed_step_keeps_60_ticks_per_second` (24 to 1000 fps → 600 ticks in 10 s) |
| 2 | Platform layer + sdl3 + headless | **Done** | `engine/platform`, `backends/sdl3`, `backends/headless`; the same client runs with a window or `--headless`; the `headless` preset builds with no SDL at all |
| 3 | Input as commands | **Done** | `Controls` latches key presses per frame and emits `Command`s per tick; `simulate` is the only mutator |
| 4 | Entities and components | **Done** | array-of-structs `World`, generational `EntityId`; the `crowd` scene has 1,000 animated sprites at 60 fps |
| 5 | Views | **Done** | Top-down, side, isometric over one 3D grid; `V` swaps the view of the same scene; draw-order tests |
| 6 | Collision and movement rules | **Done** | per-axis tile collision with sliding, gravity, jumping, lanes, kill plane; one playable scene per view |
| 7 | Assets and scenes | **Done** | `.scene` files; PNG images, textured tiles, animated sprite sheets (`views.sprite_frames_follow_the_tick`); hot reload for server and clients (`net.hot_reload_reaches_every_client`); per-scene sky; sound effects in any common format (`core.sounds_decode_from_every_format`) |
| 8 | Job system | **Done** | `JobSystem::parallel_for`; movement on worker threads with byte-identical results; `scraps_race_demo` loses ~70% of plain-counter updates and none with atomics |
| 9 | Netcode | **Mostly done** | UDP server, offline = embedded server, prediction and reconciliation, remote-player interpolation (`net.remote_players_are_interpolated_without_overshoot`), redundant inputs, fragmented snapshots, lossless delta snapshots (`net.deltas_survive_loss_and_reorder`; crowd 91 KB → ~14 KB, idle isometric 694 B → 35 B), challenge-cookie handshake and rate limits (`net.hello_flood_is_rate_limited`, protocol v4). Not yet: relevance (part B), lag compensation |

Steps 1 to 4 of the previous "Next" list (interpolation, sprites, hot reload,
job system) were done on 2026-11-12; spec: `docs/specs/2026-11-12-next-four-steps.md`.

## Next, in order

Each item: spec in `docs/specs/`, owner approves, implement, review, bench.

1. **Snapshot cost, part B (relevance): owner decision.** Part A, delta
   snapshots, was done on 2026-11-12 (spec
   `docs/specs/2026-11-12-delta-snapshots.md`): small scenes now cost 1 to
   8 KB/s per client, but the crowd scene, where all 1,000 entities move every
   tick, still sends ~14 KB per tick (~0.85 MB/s per client). Part B options:
   B1 priority by distance, B2 area of interest, B3 a lower snapshot rate
   (already `--snapshot-interval`). **Owner decision (2026-11-12): part A
   only for now; part B stays open** until a game needs hundreds of moving
   things online. Snapshots stay at 60 Hz.
2. ~~Internet hardening~~ **done** 2026-11-12: a forged Hello now gets one
   13-byte Challenge (smaller than the Hello) and no slot; replies to
   strangers are rate limited per address and in total; each client's
   datagrams are capped (`net.hello_flood_is_rate_limited`, protocol v4).
3. ~~Look and feel~~ **done** 2026-11-12: sky colour or gradient per scene,
   facing kept when a character stops, drop shadows, sound effects from any
   common audio format (`docs/decisions/2026-11-12-audio-formats.md`).
   Spec and results: `docs/specs/2026-11-12-hardening-and-look.md`.
4. ~~Mouse input~~ and ~~hot-reloadable game code~~ **done** 2026-11-12
   (spec `docs/specs/2026-11-12-mouse-and-hot-reload.md`, results at its end;
   decision `docs/decisions/2026-11-12-game-modules.md`). `Input` has the
   pointer, buttons and wheel; Views turn the cursor into a world point; a
   click is a `Command` (the sandbox places a coin within reach). Games
   describe themselves with `GameApi`; with `--hot-game` the client (offline)
   and the server load the game as a DLL and swap in each new build of
   `cmake --build --preset mingw-game` without restarting.
5. ~~Save and load, music, gamepad, pause, packaging, the React UI~~ **done**
   2026-11-14 (spec `docs/specs/2026-11-14-offline-essentials-and-ui.md`,
   results and review fixes at its end; decision
   `docs/decisions/2026-11-14-ui-layout.md`). Built by four parallel agents
   (wave 1), then three for the UI (wave 2). Quick save/load (F5/F9) and server
   `--load`/`--save`; streamed music with crossfades; first-gamepad support;
   offline pause (P / start); `-DSCRAPS_GAME`, release workflows and a zip that
   runs on a clean PC; `scraps_web.exe`, React menus and HUD in a WebView2
   window over WebGL2. Not checked by hand yet: a real gamepad, listening to
   the music, high-DPI and multi-monitor windows.
6. **Rust, as a learning track** after the above: `docs/rust-plan.md`.

## Still open (owner decides)

- ~~UI technology~~ **decided** 2026-11-14: layout A, React in a WebView2
  window over WebGL2 (`docs/decisions/2026-11-14-ui-layout.md`), built.
- **Rust** in the project at all: learning only, a Tauri UI shell, tools, or
  a port. Decided after parts 1 to 3 of `docs/rust-plan.md`.
- Scripting: none, Lua, or C++ only. (Today: C++ only, by default, not by
  decision; since 2026-11-12 that C++ reloads while the game runs.)
- The tool used to author levels.
- Whether `kTickRate` should be configurable per game.
- The Rule 4 question: core still treats +z as "up" for `on_ground` and Jump
  (`docs/architecture.md` section 5).
