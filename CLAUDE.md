# Scraps engine

A 2D C++17 game engine the owner (DZO) is building to learn engine internals.
Top-down, side-view and isometric; offline or online with 5 to 10 players on a
server the developer runs. The owner prefers visual explanations, wants design
discussed before code, and says when to implement.

## Rules of work

- **Read the constitution before any change**: `.claude/skills/engine-constitution/SKILL.md`.
  Five rules decide what is allowed: (1) only `simulate` changes the World,
  (2) fixed 60 Hz steps with interpolated rendering, (3) core never includes a
  backend, (4) core doesn't know which way is "down" (Views do), (5) the
  server is the authority and offline = client with the server embedded.
  If a task needs a rule bent, stop and ask.
- Build, test, run, benchmark: `.claude/skills/build-and-test/SKILL.md`.
  Both `mingw` and `msvc` presets must build with zero warnings and pass `ctest`.
- Review: `.claude/skills/review-checklist/SKILL.md` (ends in Go / No-Go).
- Agents (`.claude/agents/`): `architect` (spec, no code), `engine-dev`
  (approved spec → code + tests), `reviewer` (Go/No-Go), `bench` (frame times).
- New features: spec in `docs/specs/` → owner approves → implement → review → bench.
  New decisions: `docs/decisions/YYYY-MM-DD-<topic>.md`.
- Details: `docs/plan.md` (status), `docs/architecture.md` (how it fits),
  `docs/scene-format.md` (level files), `docs/rust-plan.md` (future Rust track).

## Where it stands (2026-11-12)

| Stage | Status |
|---|---|
| 1 Loop · 2 Platform (sdl3 + headless) · 3 Input as commands | Done |
| 4 Entities (1,000 in `crowd`) · 5 Three views over one grid · 6 Collision/movement | Done |
| 7 Assets and scenes | Done: `.scene` files, PNG textures, animated sprite sheets, hot reload, per-scene sky, sound effects in any common format |
| 8 Job system | Done: `JobSystem::parallel_for`, threaded movement, `scraps_race_demo` |
| Mouse input · hot-reloadable game code | Done: pointer, buttons, wheel, click → Command; `GameApi`, `--hot-game`, `*-game` build presets |
| 9 Netcode | Mostly done: UDP server, prediction + reconciliation, remote-player interpolation, offline over loopback, lossless delta snapshots, challenge-cookie handshake + rate limits (protocol v4). Not yet: relevance (part B), lag compensation |

Decided: name Scraps; array-of-structs entities; one 3D tile grid with 2:1
diamond isometric; in-house test harness + CTest; `Net` = UDP + in-memory
loopback; PNG via stb_image with sprite sheets; `simulate` takes an optional
`JobSystem*` fourth parameter (`docs/decisions/2026-11-12-simulate-jobs.md`);
audio: every common format (WAV, AIFF, FLAC, MP3, OGG Vorbis) recognised by
content, mixed by the engine (`docs/decisions/2026-11-12-audio-formats.md`);
games describe themselves with `GameApi` and can be hot-reloaded as a DLL
(`docs/decisions/2026-11-12-game-modules.md`).
Art comes from `tools/make_sandbox_art.py`, sounds from
`tools/make_sandbox_sounds.py`; audio test files from
`tools/make_audio_fixtures.py` (both need ffmpeg, installed via MSYS2).
Docs diagrams: SVGs from `tools/make_*_diagrams.py`, each with an ASCII
"Text version" under it for agents. Not a git repo yet (the owner
declined to init for now).

## Next steps, in order

1. **Cheaper snapshots, part B**: part A (lossless delta against the
   client's acked snapshot, 32-state window) is **done**; results at the end
   of `docs/specs/2026-11-12-delta-snapshots.md` (crowd 83 KB → ~14 KB per
   tick, small scenes ~20 to 140 B). Part B, relevance (B1 priority by
   distance / B2 area of interest / B3 lower rate): the owner looked at the
   numbers and chose to **leave B open** until a game needs it (2026-11-12).
   Lag compensation: spec it with the first game that has fast PvP hits.
2. ~~Internet hardening~~ and ~~look and feel~~: **done** 2026-11-12
   (`docs/specs/2026-11-12-hardening-and-look.md`, results at its end).
3. ~~Mouse input~~ and ~~hot-reloadable game code~~: **done** 2026-11-12
   (`docs/specs/2026-11-12-mouse-and-hot-reload.md`, results at its end).
   Run `scraps_client --hot-game`, edit `games/sandbox/*.cpp`, then
   `cmake --build --preset mingw-game`: the running game swaps in the new code.
4. **Rust as a learning track**: `docs/rust-plan.md` (written for someone with
   no Rust experience; starts after stage 8, which is now done).

## Open decisions (owner decides; don't pick them by writing code)

- **UI technology.** The owner wants to write UI in React/HTML/JS while still
  shipping a native exe (Tauri/Electron style, not a browser build). Two
  layouts discussed, no choice made yet:
  - **A. WebView draws everything**: native C++ engine + a WebView2 window;
    the world drawn with WebGL from per-frame data (needs shared memory, not
    JSON IPC); React for menus/HUD.
  - **B. Engine draws the world, HTML is an overlay**: native GPU rendering,
    React rendered offscreen (Ultralight or CEF) and composited on top.
  Either is a new backend (Rule 3); UI actions become `Command`s (Rule 1).
  Both need engine work first: text input (an input event list) in `Input`
  and the client loop as a one-frame function (sprites, textures and the
  mouse now exist).
- Rust in the project: learning only, a UI shell, tools, or a port
  (`docs/rust-plan.md`, part 5).
- Scripting: none, Lua, or C++ only. (Today: C++ only, reloadable while the
  game runs, `docs/decisions/2026-11-12-game-modules.md`.)
- The level-authoring tool.
- Rule 4 question: core still treats +z as "up" for `on_ground` and Jump
  (see `docs/architecture.md` section 5).
- Whether `kTickRate` should be configurable per game.
