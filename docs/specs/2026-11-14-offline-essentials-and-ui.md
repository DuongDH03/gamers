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
