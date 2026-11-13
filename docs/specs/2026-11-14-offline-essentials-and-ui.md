# Spec: offline essentials, and the UI layer

- Date: 2026-11-14
- Status: **Approved by the owner (2026-11-14), being built.** Answers: parts 1
  to 5 approved as written; UI **layout A (WebView2)**
  (`docs/decisions/2026-11-14-ui-layout.md`); P pauses and Esc quits until the
  UI has a pause menu; one commit per part on `main`, then push to `origin/main`.
- Asked for: "finish implementing all of this": save and load, music, gamepad,
  pause, packaging a release, and the UI layer. Plan it, divide it among agents,
  then commit and push.
- Diagrams: `tools/make_offline_ui_diagrams.py` → `docs/images/spec-ou-*.svg`.

Six parts. Parts 1 to 5 are engine work that every game reuses, and they are
independent of each other, so four agents build them in parallel, each in its
own git worktree (wave 1). Part 6, the UI layer, needs your choice between
layout A and layout B, and it builds on wave 1, so it comes second (wave 2).

## The build plan

![Wave 1: four engine-dev agents in parallel worktrees build save/load with pause, music, gamepad, and packaging; then a merge, one review and a bench. Wave 2: UI prerequisites, then the UI backend and React screens; review, bench, commit and push](../images/spec-ou-waves.svg)

<details><summary>Text version</summary>

```text
pre-step (me): Key gains P, F5, F9 (so agents don't all edit the same enum)

wave 1, parallel, each agent in its own git worktree / branch
  ├─ agent 1: save and load + pause      (core/save, Server/Client pause, client keys)
  ├─ agent 2: music                      (streaming decoders, mixer music channel, scene "music")
  ├─ agent 3: gamepad                    (Input, sdl3, headless, Controls, GameApi v2)
  └─ agent 4: packaging + game choice    (SCRAPS_GAME, scene lookup, install, CPack zip)
merge into main (me) ─► reviewer (Go/No-Go) ─► bench ─► commit each part

wave 2, after your A/B choice
  agent 5: UI prerequisites   (input event list, text input, client loop as one frame)
  agent 6: UI layer           (backend for A or B, React screens)
reviewer ─► bench ─► commit ─► push to origin/main
```

</details>

Each agent works on its own branch, so none can overwrite another's files.
The files several parts touch (`apps/client/main.cpp`, `games/sandbox`) get
small, separate edits that I merge. Every part must, on its own branch, build
on `mingw` and `msvc` with zero warnings and pass `ctest`.

---

## Part 1. Save and load

**What:** write the game to a file and read it back, from a key or later a
menu. Each game reuses this; it decides only *when* to save.

- **What's in a save:** the World's state (entities, tick, random state,
  players), as snapshots already serialise it (`write_world_state`), with a
  header: magic `SCRS`, save-format version, scene name, scene content hash,
  tick, and the game's name. The level is not stored; it comes from the scene
  file, as on every start.
- **New in core:** `core/save.h`. `write_save(world, scene_name, scene_hash,
  game_name, bytes)` and `read_save(bytes, header, world_from_scene, error)`.
  Reading starts from the World built from the scene and overlays the saved
  state, so rules and level stay the scene's.
- **Who saves (Rule 5):** the server, because it holds the true World.
  Offline that is the embedded server. `Server::world()` is read-only.
- **Loading (Rule 1):** like hot reload, it is construction, not mutation.
  `Server::load_state(World)` replaces the World between ticks, and clients
  get a new Welcome and a full snapshot.
- **Checks:** wrong magic or version, or a different scene name: refused with
  a message. Same scene name but a changed scene file (another hash): loaded,
  with a HUD warning, because levels change during development.
- **Where:** `SDL_GetPrefPath("Scraps", <game>)` (on Windows,
  `%APPDATA%\Scraps\<game>\`); with no SDL (headless), `./saves/`. One slot
  for now, `quick.sav`. The app decides the path; core only turns bytes into a
  World and back.
- **Keys (offline only):** F5 saves, F9 loads. The dedicated server gets
  `--load <file>`, and `--save <file>` on clean shutdown.
- **Tests:** a save round-trips byte for byte; a truncated, corrupted or
  other-version file is refused; another scene is refused; a changed scene
  warns. Offline: play, save, play on, load, and the World equals the saved
  one. After a load, the client's prediction restarts from the new snapshot.

## Part 2. Music

**What:** background music in any common format, looping, with fades. Today
sounds are capped at 30 s, because they are decoded completely into memory.
A 3-minute song would be about 70 MB decoded, so music has to stream.

- **Streaming in core:** `core/music.h`, `MusicStream`. It opens OGG, MP3,
  FLAC or WAV (recognised by content, like sounds) with the pull decoders
  already vendored (stb_vorbis, dr_mp3, dr_flac, dr_wav). It decodes a few
  thousand frames at a time, converts to 48 kHz stereo, and wraps to the
  start when looping. All buffers are sized when the stream opens, so mixing
  allocates nothing.
- **Mixer:** a music channel next to the effect voices. `play_music(stream,
  volume, fade_in)`, `stop_music(fade_out)`; starting a new track crossfades.
  `music_volume` is separate from effects.
- **Scenes:** `music <path> [volume V]` plays when the scene loads, and keeps
  playing across hot reloads if the path is unchanged.
- **Decoding runs on the main thread** while mixing ahead, as effects do now:
  about 60 ms of audio per frame, well under a millisecond.
- **Sandbox:** `tools/make_sandbox_sounds.py` composes a short original
  chiptune loop (about 30 s) as `music.ogg`. Every scene plays it.
- **Tests:** for each format, the first seconds streamed equal the same file
  fully decoded; a loop wraps with no gap or click; crossfade volumes follow
  the curve; mixing with music allocates nothing; a missing or broken file
  gives an error, never a crash.

## Part 3. Gamepad

**What:** play with a controller. Input gets the first connected gamepad;
local multiplayer with several pads is out of scope.

- **`Input`:** `GamepadButton` (south, east, west, north, d-pad, start, back,
  shoulders, stick clicks), `GamepadAxis` (left x/y, right x/y, triggers),
  `gamepad_connected()`, and button down, pressed and released this frame,
  latched like keys. Axes are −1..1, raw; the dead zone belongs to Controls.
- **sdl3:** `SDL_INIT_GAMEPAD`. Open a pad when it is added, close it when it
  is removed. Button names follow SDL's positional layout (south = A on
  Xbox, Cross on PlayStation).
- **headless:** `set_axis` and `press/release(GamepadButton)` for the
  autopilot and tests.
- **Controls:** the left stick moves like WASD but analog (Move's `vec` may
  already be shorter than 1), with a radial dead zone of 0.2. The d-pad acts
  as WASD, south jumps, and start toggles pause (Part 4).
- **`GameApi` version 2:** `GameBinding` can name a gamepad button. Version 1
  modules are refused with the usual message. The sandbox binds west to
  "drop coin".
- **Tests:** stick to move direction in all three views; the dead zone; a
  tap between ticks is emitted exactly once; a pad connected mid-game; d-pad
  equals WASD.
- **Not verifiable here** without a controller plugged in: the sdl3 path is
  checked by hand by you.

## Part 4. Pause

**What:** stop the game and resume exactly where it was. Offline only; in
an online game one player can't stop everyone else's world. That would be a
design decision per game, and the server would own it.

![Pause stops ticks on the embedded server and the client together, freezes timeouts, resets the step clock on resume so there's no catch-up burst; rendering and audio continue](../images/spec-ou-pause.svg)

<details><summary>Text version</summary>

```text
P / gamepad start ─► app: paused = !paused
paused:   Server::set_paused(true)   no ticks, no snapshots, client timeouts frozen
          Client::set_paused(true)   no prediction, no inputs sent, server timeout frozen
          FixedStepClock stops counting; frames keep drawing the last world; music ducks to 40 %
resume:   both unpause, clock.reset()  ─► next tick is the one after the paused tick (no burst)
```

</details>

- **Rule 2 holds:** a pause adds no "slower" ticks. Ticks stop, then
  continue with the next tick number. The clock is reset on resume, so it
  doesn't try to catch up on the paused time.
- **Why the server needs to know:** without it, a paused client sends no
  input, and after 5 s the server drops it as gone. So both sides freeze
  their timeouts.
- **Shown:** "Paused" on the HUD (until the UI layer draws a pause menu).
  Music ducks to 40%. Effects that are already playing finish.
- **Tests:** 10 simulated seconds of pause give no ticks and no disconnect.
  Pause and resume give, after N ticks of play, a World byte-identical to a
  run that never paused. Resuming runs no burst of ticks. Online, the client
  ignores pause.

## Part 5. Packaging a release, and choosing the game

**What:** one command makes a zip that runs on a clean Windows PC, and the
apps can be built for any game under `games/`.

- **Choosing the game:** CMake option `SCRAPS_GAME` (default `sandbox`). A
  helper `scraps_add_game(<name> SOURCES ... SCENES <dir>)` makes the static
  library, the hot-reload module and the scene-folder setting. Apps include
  one generated header that returns the chosen game's `GameApi`, so
  `apps/*/main.cpp` no longer name the sandbox. A second game is a new folder
  plus `-DSCRAPS_GAME=<name>`.
- **Finding scenes at run time:** in order, `--scene-dir`, then `scenes/`
  next to the exe, then the source folder compiled in for development.
  Today only the last exists, which breaks on another PC.
- **`cmake --install` and CPack:** the zip holds `scraps_client.exe`,
  `scraps_server.exe`, `SDL3.dll`, the compiler's runtime DLLs (MinGW:
  `libstdc++-6`, `libgcc_s_seh-1`, `libwinpthread-1`, found with
  `file(GET_RUNTIME_DEPENDENCIES)`; MSVC: the VC runtime through
  `InstallRequiredSystemLibraries`), the game's `scenes/` with art, sounds
  and music, and a short `README.txt`. It is named
  `Scraps-<game>-<version>-win64.zip`.
- **Presets:** `cmake --workflow --preset release-mingw` (and
  `release-msvc`) configure, build, test and package in one go.
- **Release builds** link the game statically. No hot-reload module and no
  `--hot-game` in the zip.
- **Test:** `package_runs` installs into a temp folder and runs
  `scraps_client --headless --frames 120` from there, with `PATH` cut down to
  Windows' own folders. If a DLL is missing, it fails.

---

## Part 6. The UI layer (needs your choice)

**What:** menus and HUD written in React/HTML/JS, in a native exe. The first
version has: a main menu (Play, Continue = load, Quit), a pause menu
(Resume, Save, Load, Settings, Quit), the HUD (coins), and settings (music
and effects volume). It shows both kinds of UI action: game actions become
`Command`s (Rule 1), for example a "drop coin" button, and app actions such
as save, pause and quit call the app.

**Prerequisites for both layouts (agent 5):**
- `Input` gains an event list per frame (key, button, text, pointer, pad),
  plus text input on and off, so a text field gets typed characters.
- The client loop becomes a one-frame function, `ClientApp::frame(now)`, so
  a host that owns the window loop (a WebView or browser engine) can call it.
- A `UiBridge` interface: the app sends a small JSON "view model" (coins,
  paused, menu state) when it changes, and receives UI actions by name.

![Layout A: a WebView2 window owns the screen; the engine's Views still produce triangle batches, which go through shared memory to WebGL; React draws menus on top in the same page. Layout B: the sdl3 window and GPU draw the world; an offscreen browser engine renders React to a bitmap that is uploaded as a texture and drawn on top](../images/spec-ou-ui.svg)

<details><summary>Text version</summary>

```text
A. WebView draws everything                         B. HTML overlay on native rendering
┌──────── WebView2 window ────────┐                  ┌────────── sdl3 window ───────────┐
│ React: menus, HUD (DOM)         │                  │ world: SDL GPU (as today)        │
│ WebGL canvas: the world         │                  │ + texture of the UI bitmap       │
│   ▲ triangle batches + textures │                  │       ▲ offscreen browser engine │
└───┼─────────────────────────────┘                  └───────┼──────────────────────────┘
    │ shared memory (no JSON per frame)                       │ Ultralight (free for small indie, closed)
native engine: simulate, server, net, audio, Views            │ or CEF (open, ~150 MB, multi-process)
```

</details>

| | A. WebView2 draws everything (recommended) | B. Native world, HTML overlay |
|---|---|---|
| How | New backend `webview2`: a Win32 window with a WebView2 control implements Window, Input and Renderer2D. `fill_triangles` batches and textures go to WebGL2 through a `CoreWebView2SharedBuffer`; React is in the same page. Audio stays native (SDL audio only). | Keep `sdl3`. Render React offscreen with Ultralight or CEF into a bitmap; `Renderer2D` gains `update_texture`; draw it over the world; route input to the UI first, the game second. |
| Reuse | All three Views unchanged: they already reduce everything to triangle batches and textures, which is all `Renderer2D` is. | Everything unchanged; the UI is added on top. |
| Dependencies | WebView2 runtime is part of Windows 11 (installed here, version 157); WebView2 SDK (MIT, NuGet). Node for building the React app (installed: Node 20). | Ultralight: closed source, free under a revenue limit, about 40 MB. CEF: open, about 150 MB, several processes. Plus Node. |
| Developer experience | Edge DevTools (F12) on the real game UI; Vite hot reload of React while the game runs. | DevTools only through a remote inspector; harder. |
| Costs | Windows only for the React build (the `sdl3` build stays as it is, without React UI). About one more frame of display latency through the browser compositor. | A big binary dependency; the UI bitmap is copied to the GPU every frame it changes; more glue code for input routing. |
| Rules | A new backend (Rule 3); UI actions are Commands (Rule 1). | The same. |

**Recommendation: A.** `Renderer2D` is only "textures plus triangle batches",
so a WebGL backend is small, and the world looks exactly as it does now.
Nothing proprietary or 150 MB is added, and React gets real browser tools.
The price is Windows only for the React UI, which matches how you ship
today.

**Agent 6 builds the chosen layout:**
- the backend;
- a `ui/` React app (Vite + TypeScript), built into `ui/dist` and shipped in
  the zip;
- the four screens above;
- tests: the bridge's JSON and actions, a UI action becoming a Command, and
  for A, the shared-buffer batch format round-trips.

The window itself is checked by hand by you.

### 6a. How wave 2 is split (decided when building, 2026-11-14)

Three agents, so the C++ and the React work can run at the same time:

- **Agent 5, engine prerequisites (C++).**
  - The `Input` event list and text input.
  - `ClientApp::frame(now)`: the client loop moved out of `main` into
    `apps/client/client_app.{h,cpp}`. The sdl3 client keeps working through it.
  - A platform interface `platform/ui.h` (`UiHost`) with a headless
    implementation for tests.
  - The app-side logic that builds the view model and handles actions, in
    `apps/common/ui_model.h`.
  - `GameApi` version 3 with named UI commands.
- **Agent 7, React app (TypeScript), in parallel with agent 5.**
  - `ui/` with Vite + React + TypeScript and the four screens.
  - A mock bridge, so it runs in a normal browser with `npm run dev`.
  - Unit tests with Vitest.
- **Agent 6, the WebView2 backend and app, after both.**
  - `engine/backends/webview2/` and the new `scraps_web` executable.
  - The WebGL renderer, input forwarding, and loading `ui/dist`.

**The bridge protocol (JSON messages).** It is the same in every build;
only the transport differs. In WebView2, native posts with
`PostWebMessageAsJson` and the page posts with
`window.chrome.webview.postMessage`.

Native → UI, sent when something changed (not every frame):

```json
{ "type": "view",
  "screen": "main" | "playing" | "paused" | "settings",
  "game": "sandbox", "hud": "coins 3", "paused": false,
  "has_save": true, "online": false,
  "music_volume": 0.8, "effects_volume": 1.0,
  "notice": "saved", "commands": ["drop_coin"] }
```

UI → native:

```json
{ "type": "action", "name": "play" | "continue" | "quit" | "pause" | "resume" |
  "save" | "load" | "open_settings" | "close_settings" |
  "set_music_volume" | "set_effects_volume" | "command",
  "value": 0.5, "command": "drop_coin" }
```

- `command` actions name a game command from `GameApi::ui_commands`
  (version 3: `{name, CommandType}` pairs; the sandbox's is `drop_coin`).
  The app turns them into a `Command` at the next tick (Rule 1).
- Every other action is an app action: save, load, pause, quit and volumes.
- Unknown or malformed messages are ignored and counted, never trusted.
- "Playing" means the game has the keyboard and mouse. In the other screens
  the UI does, and Esc opens or closes the pause menu.

---

## Rules check

| Rule | Effect |
|---|---|
| 1. Only `simulate` changes the World | Gamepad and UI game actions are Commands. Loading a save replaces the World between ticks, as a scene reload does (construction). Pause stops ticks; it never edits the World. |
| 2. Fixed step | Pause stops ticks and resets the clock; there is no variable step. Music streams by frame, like effects. |
| 3. No backend in core | Save, music decoding and mixing are pure core code. The gamepad comes through `Input`. The save folder and the UI are backends or apps. |
| 4. No "down" in core | The stick maps through the View, as WASD does. |
| 5. Server authority | The server saves and loads. Pause is offline only, and the server knows about it. |
| Code rules | No allocation per frame (streaming buffers are sized when a stream opens), no globals, no exceptions in core. |

## Out of scope

Several save slots and save thumbnails, cloud saves, local multiplayer with
several pads, rumble, rebinding controls in a menu, online pause or voting,
an installer (.msi), code signing, auto-update, macOS and Linux packages,
localisation.

## Questions for you

1. Approve parts 1 to 5 as written, built in parallel by four agents?
2. **UI: layout A (WebView2, recommended) or B (overlay)?** If B: Ultralight
   or CEF?
3. Esc: today it quits. With the UI, Esc should open the pause menu. Until
   then, P pauses and Esc still quits. Fine?
4. Push target: commit each part, then push to `origin/main`
   (`github.com/DuongDH03/gamers`) when everything is done. Or would you
   rather have a branch and a pull request?

## Results, part 3: gamepad (2026-11-14)

Built as written, with these details settled while building:

- **`Input`** gained `GamepadButton` (positional: south, east, west, north,
  d-pad, start, back, shoulders, stick clicks), `GamepadAxis` (left/right
  x/y, triggers), `gamepad_connected()`, `is_down` / `was_pressed` /
  `was_released` per button and `axis()`. Sticks use screen conventions
  (x right and y down are +1), triggers 0..1, raw. A tap inside one frame is
  both pressed and released in it, like a mouse click. No pad: every button
  up, every axis 0.
- **sdl3:** `SDL_INIT_GAMEPAD` is optional, like audio: if it fails the game
  runs without a pad. A pad already plugged in when the window opens is
  opened at once; `SDL_EVENT_GAMEPAD_ADDED` opens one if none is open;
  removing the open pad closes it and opens another that is still connected.
  Events from other pads are ignored. Focus lost clears the pad state, as for
  the mouse. **Also:** on focus gained the pad's held buttons and axes are
  read again (without "pressed"), so a stick held while switching back
  counts. Guide, paddles, touchpad and misc buttons are ignored.
- **headless:** `connect_gamepad(bool)`, `press/release(GamepadButton)`,
  `set_axis(GamepadAxis, float)`, all taking effect at the next frame.
  Presses and axes on an unplugged pad are ignored; unplugging lets go of
  everything without a "released". Axes are clamped to their range; not
  finite counts as 0.
- **Controls:** the left stick goes through a radial dead zone of 0.2,
  rescaled so the output starts at 0 at its edge
  (`Controls::apply_dead_zone`), then through the View like WASD, scaled by
  how far it is pushed: Move's `vec` has length ≤ 1. **Choices made here:**
  - A held movement key or d-pad direction wins over the stick (full speed,
    the keys' direction); the stick is used only when no direction key is
    held. Keys and stick are not added together.
  - Side view: only the sideways part of the push counts (the stick pushed
    mostly up walks slowly, not at full speed). Controls finds out by asking
    the View which screen axes have a ground direction
    (`screen_to_ground_direction` of (1, 0) and (0, 1)), so it still knows
    nothing about "down" (Rule 4).
  - The d-pad is WASD, including "d-pad up jumps" in the side view, like W.
    Pushing the stick up does not jump (it would make every slightly upward
    push a jump). South jumps in every view, like Space.
  - `bind(GamepadButton, CommandType)` shares the 12 slots with keys and
    mouse buttons and is latched once per tick the same way.
  - **Start is not wired to pause** on this branch: it is an ordinary
    `GamepadButton::kStart`. Part 4 owns pause and connects it at the merge.
- **`GameApi` version 2:** `GameBinding` gained a `pad` field (last, so the
  first three fields keep their places) and `pad_binding()`. A binding names
  exactly one of key, mouse button and gamepad button, each in range; the
  apps check that (`check_game_api`), install it into Controls and compare it
  when deciding whether a reload needs a rebind. A version 1 module is
  refused with the usual message ("built against another engine (game API
  version 1, this program has 2)"). The layout hash also mixes
  `offsetof(GameBinding, pad)`.
- **Sandbox:** west (X on Xbox, Square on PlayStation) drops a coin. The
  test marker module got the same binding.
- **Client:** Controls get the pad through `Input` as before (nothing else to
  pass); the HUD says "gamepad connected" (or "disconnected") for 3 s when the
  pad comes or goes, and prints it on the console too. The help line and
  `--help` name the pad controls. The headless autopilot plugs a pad in from
  frame 450 to 569 (stick circling, south at 470, west at 500), so every
  `client_offline_*` smoke test runs the pad path. Movement keys are held all
  through the autopilot, so the stick does not change where the walk ends.
- **Tests** (98 → 106 test functions; still 23 CTest tests):
  `platform.headless_gamepad_latches_per_frame`,
  `controls.stick_dead_zone_is_radial_and_rescaled`,
  `controls.stick_moves_the_way_it_is_pushed_in_every_view` (full and half
  push, six directions, three views, and keys win over the stick),
  `controls.dpad_equals_wasd` (all 16 combinations, three views),
  `controls.gamepad_tap_between_ticks_is_emitted_once`,
  `controls.gamepad_binding_emits_its_command`,
  `controls.gamepad_connected_mid_game`,
  `game.gamepad_bindings_are_checked_and_installed`. `game.mismatch_is_refused`
  has a third refused module that claims version 1 and checks the message
  names it. `no_allocation_in_hot_path` also samples a pad (stick across the
  dead zone, south and west pressed now and then): still 0 allocations.

Not verified: the `sdl3` gamepad with a real controller. Only the headless
path ran. To check by hand (`scraps_client`, any scene): plug a pad in while
the game runs (HUD: "gamepad connected"); the left stick walks in the
direction pushed in all three views (V switches), slowly when pushed a
little, not at all when resting; the d-pad walks like WASD; A / Cross jumps;
X / Square drops a coin when you hold one; in the side view d-pad up jumps.
Switch to another window while holding the stick and come back: the player
must not keep walking after you let go. Unplug the pad (HUD: "gamepad
disconnected", the player stops); with two pads, unplug the one in use and
the other takes over.
---

## Results, parts 1 and 4

Built 2026-11-14 on branch `worktree-agent-a7919aa9ccb12285f` (agent 1).
`mingw` and `msvc` both build with zero warnings, and `ctest` passes 28 of 28
on both (114 unit test functions, 16 of them new).

**What was built**
- `core/save.h`: `write_save`, `read_save_header`, `read_save`. Format:
  magic `SCRS`, version 1, game name, scene name, scene hash, tick, the state
  size, the state (`write_world_state`), and an FNV-1a checksum of
  everything before it. `read_save` lays the state over the World built from
  the scene, and returns `kRefused` (with a message, World untouched),
  `kLoaded`, or `kSceneChanged` (loaded, warn).
- `Server`: `world()` (it already existed, read-only), `scene_name()`,
  `scene_hash()`, `load_state(World, scene name, hash)`, `set_paused`.
  `reload` and `load_state` share one private `replace_world`.
- `Client::set_paused`. `FixedStepClock::reset()`.
- `apps/common/save_game.h` (file I/O: write to `.tmp`, then rename) and
  `apps/common/pause.h` (`OfflinePause`: the one place that pauses; it
  refuses online). `sdl3::preferences_path` wraps `SDL_GetPrefPath`.
- `scraps_client`: F5 and F9 (offline) use `quick.sav` in
  `SDL_GetPrefPath("Scraps", <game>)`, or in `./saves/` when headless; P pauses
  (offline); HUD messages for saved, loaded, errors and the changed-scene
  warning, and a centred "Paused - P to resume". `// TODO(merge): duck music`
  marks where part 2's mixer ducks.
- `scraps_server --load <file>` (the save's scene unless `--scene` is
  given) and `--save <file>` (after the loop ends).

**Tests.**
- `unit_save`:
  - format: a byte-for-byte round trip; the header; every truncation refused;
    every single-byte corruption refused; another version, scene or game
    refused; a changed scene loads with a warning.
  - offline: play, save, play on, load, and the World equals the save, tick
    included; prediction restarts after a load over 50 ms latency; saved
    players who are not here are removed; a refused load changes nothing.
- `unit_pause`: 10 simulated seconds with no ticks, no snapshots, no inputs and
  no disconnect (also with the server polled while paused); byte-identical to a
  run that never paused; no burst after resuming (50 Hz frames, and a 3 s stall
  while paused); `reset()`; online, the client ignores pause.
- Smoke tests:
  - `server_save` → `server_load`: tick 30 saved, world tick 60 after 30 more.
  - `client_offline_save_pause_load`: the headless autopilot presses F5, P, P,
    F9 and prints "saves 1, loads 1, save errors 0, paused frames 60, ticks
    while paused 0".
- `no_allocation_in_hot_path` also pauses an offline game for 600 frames and
  resumes it: 0 allocations.
- Mutation check: with the server's timeout freeze or the client's "world
  replaced" handling turned off, `pause.ten_seconds_...` and
  `save.offline_save_play_load` fail.

**Deviations, and why**
- **`load_state` takes the scene name and hash too**, like `reload`. The
  World is built from the scene file as it is now. If that file changed after
  the server loaded it, clients must be told the new hash, or they would
  refuse it.
- **A load keeps the saved tick** (reload keeps the running one), so the
  World equals the saved one. The client ignores the Welcome (same scene).
  It notices the load in the next full snapshot, whose tick is lower than the
  newest one it has. That only happens after a load, because snapshots
  complete in order. The client then clears its interpolation samples and
  starts prediction over without blending. No protocol change.
- **Saved players who are not playing now are removed at the next tick.**
  Players who are playing keep their saved entity (a join of a player who
  already has one changes nothing). Rejoining players can't be matched to
  saved ones yet, so after `--load` a dedicated server keeps no players.
- **A save from another game is refused too**, not only one for another scene.
- **A checksum** was added to the header, so corruption is always caught,
  not only when the state happens to fail validation.
- **F9 resumes a paused game**, so the loaded world shows at once.
- **The headless autopilot now presses F5 (frame 240), P (frames 270 and 330)
  and F9 (frame 420)** in every offline smoke run of 600 frames. Those runs
  write `saves/quick.sav` in their working folder (the `apps` build folder).
  The dedicated test runs in a folder of its own. Under `ctest -j` the other
  smoke runs could read each other's save; they don't fail on that, and only
  the dedicated test checks the counts.

**Not done, or not verified**
- The SDL path (`%APPDATA%\Scraps\sandbox\quick.sav`) and the HUD text were
  not seen in a window; only the headless path ran.
- `--save` runs only when the loop ends (`--ticks`). Ctrl+C stops the server
  without saving. Catching Ctrl+C needs a signal flag, which is global state,
  so that is the owner's call.
- No music ducking and no gamepad start button (parts 2 and 3; see the merge
  TODO).
---

## Results, part 5 (packaging a release, and choosing the game)

Built 2026-11-14 on its own branch (wave 1, agent 4).

**What exists now**

- `cmake/ScrapsGame.cmake`: `scraps_add_game(<name> SOURCES ... SCENES <dir>
  [DEFAULT_SCENE s] [HEADER h] [API_FUNCTION f] [INCLUDE_DIR d] [README f])`
  makes `scraps_<name>` (static), `scraps_<name>_module` (hot reload) and
  `scraps_<name>_app`, a small library with the generated `selected_game.h`
  and `.cpp` (templates `cmake/selected_game.*.in`). `selected_game()` gives
  the name, default scene, source scenes folder and the hot-reload paths;
  `selected_game_api()` the `GameApi`. `games/CMakeLists.txt` is now one call.
- `SCRAPS_GAME` (cache string, default `sandbox`) picks the game;
  `scraps_select_game()` refuses an unknown name and lists the known ones.
  `apps/*/main.cpp` no longer include `sandbox/sandbox.h`; bench and tests
  still use the sandbox directly. `scraps_add_client()` /
  `scraps_add_server()` (apps/CMakeLists.txt) build the apps for any game.
- The `mingw-game` / `msvc-game` presets build `scraps_game_module`, a
  target that depends on the selected game's module (presets cannot read a
  cache variable). Checked live: a running `scraps_server --hot-game`
  printed "game code reloaded (sandbox)" after `cmake --build --preset
  mingw-game`; `msvc-game` builds.
- Scenes at run time (`apps/common/scene_dir.h`): `--scene-dir`, then
  `scenes/` next to the exe, then the source folder. Both apps print
  `game '<name>', scenes from <dir> (<which rule>)` at start-up. The exe's
  folder comes from `dynlib::executable_directory()` (GetModuleFileNameW;
  `/proc/self/exe` elsewhere), because only `backends/dynlib` may include
  `windows.h`.
- `SCRAPS_HOT_RELOAD` (default ON). Off, the apps answer `--hot-game` with
  "--hot-game is not available in this build" (exit 2), and the hot-game
  smoke tests become `client_hot_game_unavailable` /
  `server_hot_game_unavailable`.
- `cmake/ScrapsPackage.cmake`: install rules and CPack (ZIP,
  `Scraps-<game>-<version>-win64.zip`, one top folder of the same name, in
  `build/<preset>/package/`). `cmake/ScrapsInstallRuntime.cmake` runs
  `file(GET_RUNTIME_DEPENDENCIES)` at install time with the compiler's bin
  folder searched before PATH, drops `api-ms-*`/`ext-ms-*` and anything under
  `C:\Windows\`, and fails the install on an unresolved or conflicting DLL.
  MSVC adds `InstallRequiredSystemLibraries`. `README.txt` comes from
  `cmake/README.txt.in` plus the game's own notes (`games/sandbox/README.txt`),
  with CRLF line ends.
- Presets file version 6 (needs CMake 3.25; 4.0.1 is installed):
  configure/build/test/package presets and workflows `release-mingw` and
  `release-msvc` (`build/release-mingw`, `build/release-msvc`,
  `SCRAPS_HOT_RELOAD=OFF`).
- Tests: `unit_apps` (4 functions: the three scene-folder rules, and that
  `executable_directory()` holds the test exe); `package_runs` (label
  `package`, about 2 to 7 s, so it stays in the default run): installs into
  `build/<preset>/tests/package_work/install`, checks the files and that no
  module was installed, then with `PATH=C:\Windows\System32;C:\Windows` runs
  `scraps_client --headless --frames 120` from another folder (it must say
  its scenes came from the install) and `scraps_server --ticks 30`;
  `tiny_game_runs`: `tests/tiny_game` is a second game (engine rules, one
  scene, HUD "tiny game") added with `scraps_add_game`, and
  `scraps_client_tiny` is the same client source built for it.

**Zips** (`cmake --workflow --preset release-mingw` / `release-msvc`)

| | release-mingw | release-msvc |
|---|---|---|
| File | `Scraps-sandbox-0.1.0-win64.zip`, 3,629,551 bytes | `Scraps-sandbox-0.1.0-win64.zip`, 2,067,697 bytes |
| Executables | `scraps_client.exe` 6.3 MB, `scraps_server.exe` 0.8 MB | `scraps_client.exe` 3.2 MB, `scraps_server.exe` 0.2 MB |
| Runtime DLLs | `libstdc++-6`, `libgcc_s_seh-1`, `libwinpthread-1` | `vcruntime140`, `vcruntime140_1`, `msvcp140`, `msvcp140_1`, `msvcp140_2`, `msvcp140_atomic_wait`, `msvcp140_codecvt_ids`, `concrt140` |
| Also | `README.txt`, `scenes/` (4 scenes, `art/` 10 PNGs, `sounds/` 3 files) | the same |

Each zip was unzipped into an empty folder and run from another folder with
`PATH=C:\Windows\System32;C:\Windows`: the client headless (120 frames, and
600 in `side`), the server for 30 ticks, all exit 0 with images and sounds
loaded from the zip's `scenes/`; `--hot-game` said it is not available.
With `libstdc++-6.dll` removed from a MinGW install the client exits at once
with `0xc0000135` (no dialog box), which is how `package_runs` fails.

**`-DSCRAPS_GAME=tiny`** (a headless MinGW build in `build/tiny-headless`):
`scraps_client --headless --frames 120` and `scraps_server --ticks 30` run
the tiny game in its `field` scene, and `--hot-game` loads
`scraps_tiny_module.dll`.

**Test results** (26 CTest tests, 102 unit test functions)

| | Build | ctest |
|---|---|---|
| `mingw` | 0 warnings | 26/26 passed |
| `msvc` | 0 warnings | 26/26 passed |
| workflow `release-mingw` | 0 warnings | 26/26 passed, zip made |
| workflow `release-msvc` | 0 warnings | 26/26 passed, zip made |

**Deviations from the text above**

- **No `SDL3.dll`.** The bundled SDL3 is built as a static library
  (`cmake/ScrapsSDL3.cmake`), so there is nothing to ship. If a shared SDL3
  is found installed instead, the runtime-dependency step ships its DLL.
- **The hot-reload module is still built in release build trees**, because
  `unit_game` and the allocation test load it; it is never installed, and
  `package_runs` checks that. With `SCRAPS_BUILD_TESTS=OFF` and
  `SCRAPS_HOT_RELOAD=OFF` it is not built at all.
- **The default scene moved into the game** (`DEFAULT_SCENE`, else the first
  `.scene` file), since "isometric" is the sandbox's. The sandbox's four
  named smoke tests run only when `SCRAPS_GAME` is `sandbox`; another game
  gets `client_offline_default`.
- **The tiny game builds only with tests on** (it lives in `tests/`), so
  `-DSCRAPS_GAME=tiny` needs `SCRAPS_BUILD_TESTS=ON` (the default).
- **README keys** list what exists on this branch. Pause (P), quick save and
  load (F5, F9) and the gamepad come from parts 1, 3 and 4; add them to
  `cmake/README.txt.in` when merging.
- `workflow` presets cannot pass `-D` options, so to reuse an SDL3 source
  folder, configure the preset once with `-DFETCHCONTENT_SOURCE_DIR_SDL3=...`
  and then run the workflow, which keeps the cache.

**Not verified:** a really clean Windows PC. On this one the VC runtime is
also in `System32`, so the minimal-PATH run proves the MinGW DLLs are
complete but not the MSVC list (the zip carries the VC runtime next to the
exe, which Windows loads first). The SDL3 window from an unzipped release was
not opened (headless runs only).
---

## Results, part 2 (music)

Built 2026-11-14 on branch `worktree-agent-a07bb4d37e41aa54e` (from `128e5b2`).

**What exists now**

- `core/music.h`, `MusicStream`: opens OGG Vorbis, MP3, FLAC, WAV (and AIFF),
  recognised by content; decodes 4,096 source frames at a time through a new
  pull face on the vendored decoders (`decoders::PullDecoder` in
  `third_party/audio_decoders.h`); converts to 48 kHz stereo float; loops by
  rewinding the decoder while the resampler carries on, so the last frame
  leads straight into the first. `open_music(path, ...)` reads a file.
- `core/audio_convert.h`: the downmix and the linear resampler, moved out of
  `core/sound.cpp` so effects and music share one conversion. A stream equals
  a whole-file decode sample for sample.
- `Mixer`: `play_music(stream, volume, fade_in)` (the old track crossfades
  out over the same time), `stop_music(fade_out)`, `set_music_track_volume`,
  `music_playing`, `music_tracks`, `current_music`, and separate volumes:
  `set_music_gain` (glides over 50 ms, for the player's setting times a pause
  duck: **pause should call `set_music_gain(0.4f)` and `set_music_gain(1.0f)`**)
  and `set_effects_gain`. Fades are equal power (sin in, cos out). Even an
  instant stop fades over 10 ms. `Mixer::clear()` (a scene reload) leaves
  the music alone.
- Scene line `music <path> [volume V]` in `Level::music` (`MusicDef`), same
  path rules as `sound`, at most one per scene; in the World's bytes
  (`kWorldFormatVersion` 3 → 4); documented in `docs/scene-format.md`.
- `views/scene_music.h`, `SceneMusic`: the client calls `update()` with each
  new world. Same file: keeps playing (only the volume follows the scene).
  Another file: 1.5 s crossfade. None: 1 s fade-out. First track: 1 s fade-in.
  A file that fails is printed and shown on the HUD, and the game stays silent.
- `apps/client/main.cpp`: three small edits (a `SceneMusic`, its `update()`
  next to the sound bank's, and a `music:` line in the headless summary).
- `tools/make_sandbox_sounds.py`: an original 30-second chiptune (16 bars at
  128 BPM in A minor: pulse lead, thin arpeggio, triangle bass, noise drums;
  melody written for this, not taken from anything). Notes that ring past the
  end wrap to the start, so the loop has no seam of its own.
  `games/sandbox/scenes/sounds/music.ogg`: 108,498 bytes, mono, 22,050 Hz,
  exactly 661,500 samples (30.000 s). Every sandbox scene plays it at volume
  0.6.

**Tests** (7 new, in `tests/test_music.cpp`; 105 unit test functions now)

| Test | Shows |
|---|---|
| `core.music_streams_equal_full_decode` | 12 files, every format and rates 22.05 to 48 kHz, 1, 2 and 6 channels: a stream read in pieces of 1 to 4,097 frames equals `decode_sound` exactly, looping or not |
| `core.music_loops_without_a_gap` | a synthetic loop at 48 kHz repeats exactly and its seam is no steeper than the sine; at 44.1 kHz the resampled seam is no steeper either; OGG, MP3, FLAC, WAV at 48 kHz: the second pass equals the first; a 40 s OGG and a 35 s MP3 (refused as effects) stream past 30 s and loop |
| `core.music_rejects_bad_files` | missing, not audio, empty, Opus, AAC, no samples, a loop under 1,024 frames, lying OGG comment lengths, and about 950 cut, corrupted and noisy files: errors, never a crash, samples always in −1..1 |
| `core.mixer_crossfades_music_on_the_curve` | constant-level tracks: the crossfade follows 0.5·cos + 0.25·sin to 1e-5, stop follows cos, the 10 ms minimum fade, effects and music gains apart, the duck glides, `clear()` keeps the music |
| `scene.music_line_parses_and_round_trips` | the line, its errors, and the World's bytes with and without music; hostile paths and volumes refused |
| `views.scene_music_keeps_playing_across_reloads` | same file: same stream, no crossfade; another file crossfades; a missing file fades out and reports; no line stops |
| `views.bundled_music_plays_in_every_scene` | each sandbox scene's music opens and is audible after the fade-in |

`no_allocation_in_hot_path` now also streams music while mixing: it starts
four tracks (OGG, MP3, FLAC, WAV; the short ones wrap many times, so every
decoder's rewind runs), crossfades, ducks and stops. 0 allocations.

Cost: mixing 60 ms of the sandbox music (what the client does per frame)
took 0.11 ms on average and 0.44 ms at worst (MinGW Release, 1,000 calls,
on a machine busy with other builds).

**Deviations and findings**

- **"Streams" means the file is kept in memory still encoded** and decoded
  while it plays. stb_vorbis can only read from memory or a `FILE*` (and the
  decoders are built without stdio), so reading the disk while playing
  would have meant a different decoder for OGG. A 3-minute OGG is about
  3 MB; the cap is 64 MB per music file.
- **A crash in stb_vorbis 1.22, also reachable through sound effects:** it
  trusts the length fields of an OGG file's comment header. A damaged file
  made it write outside its memory, or free pointers it never set. Found by
  the new damaged-file test. The vendored file is unchanged; instead
  `audio_decoders.cpp` walks the Ogg pages and checks that header before
  stb_vorbis sees any OGG file (music and effects). A regression case in
  `core.music_rejects_bad_files` crashes without the check. Afterwards,
  66,000 more damaged files (all formats, a scratch fuzzer, not committed)
  ran through `decode_sound` and `MusicStream` without a crash.
- dr_wav, dr_flac and dr_mp3 allocate through callbacks that use
  `operator new`, so the allocation test would see them; they allocate only
  when a stream opens. stb_vorbis keeps its default allocation (malloc in
  open, stack scratch while decoding), the mode upstream fuzzes.
- AIFF is accepted as music too (dr_wav reads it anyway).
- The World's bytes changed (`kWorldFormatVersion` 4). The network protocol
  did not change: clients load the level from their own scene file.
- `.claude/skills/build-and-test/SKILL.md` still says 98 unit test functions
  and lists no music tests; it was left alone (`.claude/**` is off limits
  for this part).

**Not verified here:** how it sounds. No one listened. What was checked
instead: the decoded `music.ogg` is exactly 30.000 s; its seam (last sample
to first) changes by 0.036, inside the track's normal sample-to-sample range
(99th percentile 0.065); peak 0.81; the headless client reports the music
playing and wrapping; the crossfade and duck curves are checked sample by
sample.

Build and tests: `mingw` and `msvc` both build with zero warnings, and
`ctest` passes 23 of 23 on both. (One MinGW run had `server_runs` fail while
other agents' test runs were going at the same time, most likely the same
fixed UDP port; it passed alone and in a full rerun.)

---

## Results, part 6 (agent 5, prerequisites)

Built 2026-11-14 on branch `worktree-agent-a1edc0c837197969c` (from
`cbd2527`). `mingw` and `msvc` both build with zero warnings, and `ctest`
passes 32 of 32 on both (149 unit test functions, 16 of them new; the new
CTest test is `unit_ui`).

**What exists now**

- **`Input` event list and text input** (`platform/input.h`).
  `Input::events()` returns this frame's `InputEventList`: `InputEvent`s in
  the order they happened (key down/up with `repeat`, UTF-8 text, pointer
  move/down/up with the position, wheel, gamepad button down/up). It holds
  256 events in fixed storage; more are dropped and counted (`dropped()`).
  Pointer moves in a row are merged into the last one. Text arrives only
  between `start_text_input()` and `stop_text_input()`, split into pieces of
  at most 31 bytes between code points; bytes that are not UTF-8 are skipped.
  `Key` gained `kBackspace`, `kDelete`, `kHome`, `kEnd` (for text fields). The
  polled API is unchanged. sdl3 fills the list from its events
  (`SDL_EVENT_TEXT_INPUT`, `SDL_StartTextInput(window)`); headless records an
  event for every call that changes something, plus `type_text()`.
- **`platform/ui.h`**: `UiMessageList` and `UiHost` (below), and
  `PlatformServices::ui`. Headless has `HeadlessUi` (`HeadlessBackend::ui()`):
  it records posts and lets a test `inject()` messages; it shows no UI until
  `set_present(true)`.
- **`apps/common/ui_json.h`**: `JsonWriter` (into a caller's buffer) and
  `JsonObject` (one flat object: string, number, true, false, null; at most 16
  fields, keys up to 31 bytes, strings up to 255). Anything else is
  malformed: nesting, comments, trailing commas, duplicate keys, invalid
  UTF-8, lone surrogates, `\u0000`, numbers that overflow, text after the
  object. No allocation.
- **`apps/common/ui_model.h`**: `UiModel`: parses actions, keeps the UI's own
  state (main menu or started, settings open, volumes, the online pause menu),
  queues game commands, builds the view message and posts it only when its
  text changed. App actions come back as `UiRequest`s, which `ClientApp`
  carries out with the same code as its keys. `NoInput` is an `Input` that
  reports nothing.
- **`apps/client/client_app.h`**: `ClientApp` (below). `main.cpp` is now
  option parsing plus a ten-line loop for sdl3 and headless. The CMake helper
  `scraps_add_client(<target> <game>)` builds `<target>_lib` (ClientApp) and
  the exe; tests link `scraps_client_lib`, and `scraps_ui_model` holds the
  JSON and the UI model.
- **`GameApi` version 3**: `GameUiCommand {const char* name; CommandType
  command;}`, and `GameApi::ui_commands` / `ui_command_count` at the end of the
  struct (at most `kMaxGameUiCommands` = 8). `check_game_api` refuses: more
  than 8, a null list with a count, a name that is not 1 to 31 ASCII letters,
  digits, `_` or `-`, the same name twice, and `kNone` or server-only
  commands ("UI command N of the game is not valid"). `GameModule` copies the
  names like the bindings (`ui_command_count()`, `ui_command(i)`,
  `find_ui_command(name, size, out)`), so a swap brings the new module's. The
  layout hash mixes `sizeof(GameUiCommand)` and the new offsets. The sandbox
  exports `drop_coin` → `kDropCoin`; the marker module exports `drop_coin` and
  `jump`; the bad "old" module now claims version 2; the tiny game has none.

**The interfaces agent 6 implements and uses**

`engine/platform/ui.h`, the UI backend's side:

```cpp
namespace scraps {
class UiMessageList {   // fixed storage: 32 messages, 2048 bytes each, 16384 in all
 public:
  static constexpr std::size_t kMaxMessages = 32, kMaxMessageBytes = 2048, kMaxBytes = 16384;
  void clear();
  bool push(const char* data, std::size_t size);  // false (counted in dropped()) if it does not fit
  void append(const UiMessageList& other);
  std::size_t count() const;
  const char* data(std::size_t i) const;           // followed by a '\0'
  std::size_t size(std::size_t i) const;
  std::size_t dropped() const;
};

class UiHost {
 public:
  virtual ~UiHost() = default;
  virtual bool present() const = 0;                              // fixed for the host's life
  virtual bool post(const char* json, std::size_t size) = 0;     // native → UI; the host copies it
  virtual void receive(UiMessageList& out) = 0;                  // UI → native since the last call; clears `out` first
};

struct PlatformServices {  // platform/services.h
  Window* window; Input* input; Renderer2D* renderer; Audio* audio;
  UiHost* ui = nullptr;    // new: a UI, or nullptr
};
}
```

Rules for a `UiHost`:
- `post` is called only when the view changed, so **the host keeps the last
  message and sends it again whenever the page (re)loads** (first load, F5 in
  DevTools, Vite hot reload). It may be called before the page is ready.
- Messages the page sends (`window.chrome.webview.postMessage`) are pushed
  into a `UiMessageList` inbox as UTF-8 and handed over by `receive`, which
  must not allocate when nothing arrived. Too long or too many: drop and
  count; never cut a message.
- Everything happens on the thread that calls `Window::pump_events()`.

`engine/platform/input.h`, which the backend fills from WebView2's input:
`Input::events()` (`InputEventList`, `push` and `push_text` do the
merging, splitting and UTF-8 checks), `start_text_input()`,
`stop_text_input()` and `text_input_active()`, plus the polled state as
today. Positions are renderer output pixels.

`apps/client/client_app.h`, what the `scraps_web` app drives:

```cpp
namespace scraps_apps {
struct ClientOptions { std::string scene, scene_dir, connect, view; bool headless = false; long frames = 0;
                       long threads = -1; bool hot_game = false; std::string save_dir; };
class ClientApp {
 public:
  ClientApp(const ClientOptions& options, scraps::PlatformServices platform,
            scraps::headless::HeadlessInput* autopilot /* nullptr */);
  bool start(std::string& error);                   // game code, scene, session
  bool frame(double now, double wall_seconds);      // false: stop (close, Esc/quit, frame limit, failure)
  bool frame(double now);                           // real time: wall_seconds = now
  int finish();                                     // goodbye; the exit code
  const ClientAppStats& stats() const; bool has_ui() const; const UiModel& ui() const;
  const scraps::Client& client() const; const scraps::Mixer& mixer() const;
  bool paused() const; const char* view_name() const;
};
}
```

`scene_dir` must already be resolved (`common/scene_dir.h`, as `main.cpp`
does); an empty `scene` means the game's default. The app keeps fixed
buffers for the bridge (about 50 KB), so create it on the heap.

**The protocol as built** (6a, with what was settled while building)

- View, posted when its text changes:
  `{"type":"view","screen":"main","game":"sandbox","hud":"coins 0","paused":false,"has_save":false,"online":false,"music_volume":1,"effects_volume":1,"notice":"","commands":["drop_coin"]}`.
  Always every field, in this order. Numbers are the shortest form (`1`,
  `0.75`). `notice` is `""` when none is shown, and at most 255 bytes.
  `has_save` is false online.
- Screens: `main` until play/continue works (nothing ticks, the network is
  not started); `playing`; `paused` (offline: the game is paused; **online:
  the pause menu over a running game**, `"paused":false`); `settings` (over
  any of them; `close_settings` goes back).
- Actions that make no sense now are ignored and counted (`ignored()`):
  `play`/`continue` once started, `pause` outside `playing`, `resume`
  outside `paused`, `save`/`load` on the main menu, and **`command` outside
  `playing`** (a game button in a menu does nothing, now or later).
  `continue` loads the quick save; if that fails, the main menu stays and the
  notice says why. `load` resumes a paused game, as F9 does. Volumes are
  clamped to 0..1.
- **Esc, P and gamepad start are handled natively** while a UI is shown: Esc
  goes playing → pause menu → playing, and settings → back; P and start only
  toggle the pause menu. **The React app should not also send `pause` or
  `resume` for Esc**, or it would undo the native toggle. Without a UI, Esc
  still quits and P pauses.
- Outside `playing`, the game controls get no input, and V, zoom, F5 and F9
  do nothing.

**Also in this part** (review findings S3 and M5 from wave 1)

- Game controls get no input while paused or while a menu is shown:
  `Controls` samples `NoInput` then. New `Controls::clear_latches()` is called
  whenever the game gains or loses the input (resume, play, continue, a load,
  a menu opening), so a press made while paused never acts after resuming,
  even one made in the same frame as P.
- Music gain = the player's music volume × the pause duck
  (`kPauseMusicDuck` = 0.4, `common/pause.h`); effects gain = the effects
  volume. Without a UI both volumes stay 1, so the gains are as before.

**Tests** (`tests/test_ui.cpp`, 16 functions)

| Test | Shows |
|---|---|
| `platform.event_list_keeps_order_and_counts_overflow` | 11 events of every kind in call order, two moves merged, overflow past 256 counted and polling still sees everything, one frame only |
| `platform.text_input_is_split_and_checked` | off: no text; 100 bytes of 1- to 4-byte characters split between code points and joined back equal; invalid UTF-8 skipped; a full list counts the drop |
| `ui.json_reader_takes_flat_objects` / `ui.json_reader_refuses_hostile_input` | escapes, surrogate pairs, numbers; 45 hostile texts and every limit refused; 20,000 damaged and random messages, never a crash |
| `ui.json_writer_escapes_and_reads_back` | quotes, control characters, a cut character becomes `?`, reads back, never writes past its buffer |
| `ui.actions_parse_and_odd_ones_are_refused` | every action name; malformed and unknown cases |
| `ui.commands_are_checked_and_copied` | GameApi v3 validation, and the names copied |
| `ui.each_action_does_what_it_says` | every action on every screen, offline and online; Esc and P; commands queued once, for the player |
| `ui.volumes_are_not_clobbered_by_the_duck` | volume × duck, both ways |
| `ui.view_is_posted_only_when_it_changes` | the exact first message; one post per change, none otherwise; a host that cannot post is retried |
| `ui.malformed_messages_are_counted_not_trusted` | malformed, unknown and dropped counted; good messages still work |
| `ui.client_app_main_menu_play_pause_save_resume_quit` | `ClientApp` with a headless UI: main menu (no ticks, one post), play, `drop_coin` sent at the next tick, pause, save (same code as F5), F5 ignored in the menu, resume, Esc in and out of the pause menu, quit, exit code 0 |
| `ui.client_app_continue_loads_the_quick_save` | continue without a save stays on the menu; with one it loads and plays |
| `ui.client_app_volumes_follow_the_ui` | volumes reach the mixer; P pauses and ducks; resume restores |
| `ui.input_while_paused_or_in_a_menu_never_acts` | Space, E, click, pad south and west while paused (also in the same frame as P), on the main menu, in the pause menu and in settings: no command afterwards; the same presses while playing do send commands |
| `ui.client_app_without_a_ui_plays_at_once` | no UI: plays at once, nothing posted, gains 1, F9 resumes a pause, Esc quits |

`game.swap_runs_the_new_code` also checks that the swapped module brings its
two UI commands (copied); `game.mismatch_is_refused` expects "(game API
version 2, this program has 3)". `no_allocation_in_hot_path` also polls a
headless UI, parses a malformed message and an ignored command now and
then, publishes the unchanged view and applies the volumes every frame: 0
allocations. Mutation checks: with the controls sampling the real input
while paused, or without `clear_latches()`, `ui.input_while_...` fails.

**Behaviour compared with `cbd2527`.** The headless client of `cbd2527`
(built from an export of that commit) and this one print the same output
for `isometric`, `top-down` (600 and 1,500 frames), `crowd` and `side
--no-autopilot`. In `side` one fewer sound plays (9 instead of 10): the
autopilot presses Space at frame 290, during its pause, and that jump no
longer happens after resuming (finding S3). With the fix reverted the count
is 10 again.

**Deviations, and why**

- The interface is `UiHost` (6a's name; part 6 said `UiBridge`), with
  `receive(UiMessageList&)` rather than a per-message poll, so a frame's
  messages arrive at once and in order with no allocation.
- `ClientApp::frame(now, wall_seconds)` has a second time: the hot-reload
  file check needs real time when `now` is simulated. `frame(now)` passes
  `now` for both.
- The online pause menu (screen `paused` with `"paused":false`) and native
  Esc handling were not in 6a; they follow from "pause is offline only" and
  "Esc opens or closes the pause menu".
- `Key` gained four keys for text fields.
- Without a UI, controls now ignore presses made while paused (finding S3);
  everything else is as before.
- With a UI the debug HUD is still drawn by the renderer; the backend or a
  later change may hide it.

**Not verified**

- The sdl3 text input and event list with a real keyboard, IME and mouse:
  only the headless paths ran. To check by hand, a small debug print of
  `events()` in a window is enough; nothing in `scraps_client` reads the
  list yet.
- No WebView exists yet, so the protocol has met no real page; agent 7's
  React app was built against 6a at the same time.
