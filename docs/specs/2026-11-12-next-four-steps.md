# Spec: interpolation, sprites, hot reload, job system

- Date: 2026-11-12
- Status: **Implemented** 2026-11-12. Approved by the owner the same day
  ("continue executing the plan", steps 1 to 4). Owner decisions taken for
  this spec: PNG images decoded with stb_image; sprite sheets with animation.
  The job-system parameter on `simulate` was accepted by the owner the same day.
- Built on both compilers with zero warnings; CTest 16/16 and 56 unit tests
  pass. Checked by hand in the SDL3 window: textured tiles and animated
  sprites in all three views, and the crowd scene at 60 fps.
- Small additions while implementing: protocol version 2 (snapshot fragments
  carry the scene hash, so a client that missed a reload's Welcome asks again),
  a translucent strip behind the HUD, and gentler background-lane shading in
  the side view.
- Plan items: `docs/plan.md`, "Next", items 1 to 4.

## Step 1. Remote-player interpolation

**Problem.** The client predicts the whole world. For other players it does
not know their inputs, so it keeps them moving with their last input. When
they stop or turn, the next snapshot snaps them back.

**Design.** Other players are drawn slightly in the past, between two real
snapshots, instead of guessed.

```
server snapshots:   ●───────●───────●───────●        (ticks 100, 101, 102, 103)
                                  ▲
render time for remote players = newest snapshot − delay (default 4 ticks ≈ 67 ms)
→ always between two known positions: no guessing, no overshoot
```

- The client keeps the last 16 snapshots' player positions (tick, entity id,
  position): a few hundred bytes.
- An interpolation clock advances one tick per client tick and is nudged
  toward "newest snapshot tick − delay" whenever a snapshot arrives (snapped
  if it is more than 10 ticks off).
- `Client::display_world(alpha)` returns a world ready to draw: a copy of the
  predicted world where the local player and every other entity are blended
  between the last two predicted ticks, and other players are placed at the
  interpolated past position. Views draw it as-is (`alpha` 0). Copying keeps
  the reserved capacity, so it does not allocate.
- Only drawing changes. Simulation, prediction and the server are untouched
  (Rule 1).

**Tests.** A remote player that moves and then stops is never drawn past the
point where the server stopped it (no overshoot), and its drawn movement has
no jumps larger than one tick of motion, with 100 ms latency.

## Step 2. Sprites and textures

**Interfaces.**

```cpp
// platform/renderer2d.h
using TextureId = std::uint32_t;            // 0 = no texture
virtual TextureId create_texture(int w, int h, const std::uint8_t* rgba) = 0;
virtual void fill_triangles(const Vertex2D*, std::size_t count, TextureId texture) = 0;
struct Vertex2D { float x, y; Color color; float u, v; };   // u, v in 0..1
```

- **Decoding** (`core/image.h`): `load_png(path, Image&, error)` reads the file
  itself and decodes with stb_image (`engine/third_party/stb_image.h`, pinned
  commit, PNG only, compiled in its own target without our warning flags).
  Pure CPU, no renderer: the server never loads images.
- **Scene format:** `image <name> <path> [frames N] [size W H] [fps F]` declares
  an image or sprite sheet (frames in one row, each W x H pixels);
  `tile ... texture <image>` gives a tile a texture; `archetype ... sprite <image>`
  gives an entity a sprite. Image paths are relative to the scene file, with
  no `..`. Level gains `images`; `TileDef` and `Archetype` gain an image index
  (plain data; serialized; the world format version goes to 2).
- **Loading** (`views/sprite_bank.h`): `SpriteBank::load(level, scene_dir,
  renderer)` decodes every image of a level and creates textures; Views ask it
  for an image's texture. Missing files are reported and fall back to flat
  colours, so art never stops the game.
- **Drawing:**
  - Blocks: every face is textured with the tile's image, shaded by the face
    shading (top 100%, sides 78% and 62%). Faces are parallelograms on screen
    in all three views, so two triangles map the texture exactly.
  - Entities with a sprite are drawn as an upright picture standing on the
    entity's feet, sized to the entity's box width. In isometric and side
    views it faces the camera ("billboard").
  - Animation: frame = (world tick x fps / 60) mod frames while the entity
    moves, frame 0 when it stands still. It comes from the tick, so it is the
    same on every machine. Sprites flip horizontally when moving left on screen.
  - Batching: triangles go to the renderer in painter's order; a new batch
    starts only when the texture changes.
- **Art:** `tools/make_sandbox_art.py` generates the sandbox's pixel art
  (tiles, a 4-frame walking hero, a spinning coin, a critter) as PNGs into
  `games/sandbox/scenes/art/`. It uses only Python's standard library.

**Tests.** PNG decode of a generated file; bad and missing files fail cleanly;
scene `image`/`texture`/`sprite` parsing and errors; textured batches split
only on texture change; animation frame from the tick; `World` with images
round-trips through bytes.

## Step 3. Scene hot reload

- `core/file_watch.h`: `FileWatcher(path)`, `bool changed()` compares the
  file's modification time (the file system, not the clock). Apps call it
  about twice a second.
- `Server::reload(World, name, hash)`: replaces the world (keeping the tick
  number, so ticks never go backwards), re-joins every connected player
  through the usual join command, and sends every client a new Welcome.
- Client: a Welcome with a different scene hash while connected means
  "reload": load the scene again, rebuild the worlds, clear the
  interpolation buffer. Its SpriteBank reloads images too.
- A scene with an error is not applied: the old world keeps running and the
  error is printed (and shown on the offline client's HUD).
- Apps: `scraps_server` and the offline client watch their scene file.

**Tests.** After a reload, both clients see the new tiles and still have their
players; a broken edit leaves the world running.

## Step 4. Job system (stage 8)

```
main thread                       worker threads (cores − 1)
  parallel_for(1000 entities, 64) ──► chunk 0..63   chunk 64..127  ...
  also takes chunks ─────────────────►    (each grabs the next chunk with an atomic counter)
  waits until every chunk is done ◄───────┘
```

- `core/jobs.h`: `JobSystem(worker_count)` starts threads once;
  `parallel_for(count, grain, fn)` splits `[0, count)` into chunks, runs them
  on the workers and the calling thread, and returns when all are done. No
  allocation per call; no `std::function` (a function pointer and a context
  pointer, wrapped by a template for lambdas).
- **Movement in parallel:** each entity's step reads the level (never
  changed during a tick) and writes only itself, so entities split cleanly
  across threads. Removing entities and respawning players touch shared
  lists, so they run afterwards on one thread. The result is identical to
  the single-threaded run, byte for byte.
- **Passing the job system (proposed decision for the owner):**
  `simulate(World&, const CommandList&, Tick, JobSystem* jobs = nullptr)`.
  The three-argument call in Rule 1 still works and still means the same
  thing; the extra argument only says how to run, not what changes.
  `SimulateFn` gains the same parameter, so games pass it on. Recorded in
  `docs/decisions/2026-11-12-simulate-jobs.md` as **Proposed**.
- **The race lesson:** `scraps_race_demo` increments one counter from every
  core, once with a plain variable and once with `std::atomic`. The unsafe run
  loses updates and exits with failure; CTest registers it as "expected to
  fail" (skipped on a single-core machine), and the atomic run must pass.

**Tests.** Parallel and single-threaded simulate give identical bytes for the
crowd scene; `parallel_for` covers every index exactly once for many sizes
and grains; no allocation with jobs; the race demo as above. The bench prints
single-threaded and parallel simulate times.

## Out of scope

Snapshot delta compression, internet hardening (plan items 5 and 6), sound
assets, a level editor, and the open UI-technology decision.
