# Spec: cheaper snapshots (delta encoding, then relevance)

- Date: 2026-11-12
- Status: **Part A approved and implemented (2026-11-12).** Part B: after
  seeing part A's results, the owner chose to leave it open until a game
  needs it (2026-11-12). Snapshots stay at 60 Hz.
- Plan item: `docs/plan.md`, "Next", item 1 (snapshot cost).

## Goal

Make online play affordable beyond a LAN. Today every client gets the whole
world state every tick, whether anything changed or not. After part A, a
client only receives what changed since the last snapshot it confirmed. After
part B (a separate decision), it also receives far-away things less often.

## The problem, measured

`scraps_bench`, full snapshot per tick, per client:

| Scene | Entities | Snapshot | Datagrams | At 60 Hz, per client | 10 players, server upload |
|---|---|---|---|---|---|
| isometric | 6 | 646 B | 1 | 39 KB/s | 0.4 MB/s |
| top-down | 15 | 1,393 B | 2 | 84 KB/s | 0.8 MB/s |
| crowd | 1,001 | 83,231 B | 82 | 5.0 MB/s | 50 MB/s |

A home connection uploads maybe 2 to 20 MB/s. Small scenes are fine already;
anything with hundreds of moving things is not. And most bytes are repeats:
in `top-down`, coins and walls do not change from one tick to the next, yet
each one is resent 60 times a second.

## How delta snapshots work

```
server                                             client
──────                                             ──────
snapshot 40 (full) ───────────────────────────────► has 40
                     ◄──────── Input (…, "I have 40")
snapshot 41 = changes since 40 ───────────────────► 40 + changes = 41
snapshot 42 = changes since 40 (41 not acked yet) ─► 40 + changes = 42
                     ◄──────── Input (…, "I have 42")
snapshot 43 = changes since 42 ───────────────────► 42 + changes = 43
```

- Each client tells the server, in every Input packet, the newest snapshot it
  has completely: its **acked snapshot**, the "baseline".
- The server encodes each client's snapshot as "baseline + changes". It keeps
  the last 32 world states, so it can build a delta against any baseline a
  client might still hold.
- The client keeps the last 32 states it rebuilt, finds the baseline named in
  the delta, applies the changes, and gets a state byte-for-byte equal to the
  server's. Prediction and reconciliation work exactly as today, on a full
  world (Rule 5's server authority is unchanged).
- **Lost packets are harmless**: if snapshot 41 is lost, 42 is still a delta
  against 40, which the client has. Nothing is ever resent. This is why the
  baseline is "what the client confirmed", not "what was sent last".
- **Fallback to full**: no ack yet (just joined, or just reloaded), or the
  acked snapshot fell out of the 32-state window (a long stall): the server
  sends a full snapshot, exactly as today.

### What "changes" means: the encoding

An entity is ~83 bytes of fields. They change at very different rates, so
each changed entity carries a small bitmask saying which groups follow:

| Group | Fields | Changes when |
|---|---|---|
| slot | `alive`, `generation` | spawn / destroy |
| pose | `position` | every tick it moves |
| motion | `velocity`, `on_ground` | on jumps, bounces, start/stop |
| control | `player`, `move_intent` | when a player presses keys |
| look | `archetype`, `kind`, `flags`, `size`, `color`, `move_speed` | almost never |
| game | `user[4]` | game events (coin count) |

```
delta = baseline id | tick | rng | slot count
      | changed entities: (skip N unchanged slots, varint) (group mask, 1 byte) (changed groups…) …
      | free list: unchanged flag, or the whole list
      | player table: unchanged flag, or the whole table
```

Lossless: no rounding, so the client's rebuilt state is exactly the server's
(rounding positions to 16 bits would save more, but makes the client's
prediction start from slightly wrong numbers; left as a later option).

### Expected sizes (estimates, to be measured by the tests)

| Scene, steady state | Full | Delta | Why |
|---|---|---|---|
| isometric / top-down, players standing | 0.6 to 1.4 KB | ~20 to 30 B | header only |
| top-down, one player walking | 1.4 KB | ~40 B | one pose + control group |
| crowd, 1,000 wanderers moving | 83 KB | ~16 KB | ~16 B per moving entity; velocity only changes on bounces |

So delta alone makes small and medium scenes nearly free, but a scene where
*everything* moves stays expensive: that is what part B is for.

### Interfaces

```cpp
// core/serialize.h: pure functions, no networking
void write_world_delta(ByteWriter&, const WorldState& baseline, const World& current);
bool read_world_delta(ByteReader&, const WorldState& baseline, World& out);   // validated

// core/world.h: the part of a World a snapshot carries (no Level),
// copyable without the level's tiles
struct WorldState { Tick tick; std::uint64_t rng_state; std::vector<Entity> entities;
                    std::vector<std::uint32_t> free_slots; std::array<EntityId, kMaxPlayers> player_entities; };
```

- `net/protocol`: version 3. Input gains `u32 acked_snapshot`. The snapshot
  payload starts with `u32 baseline_snapshot` (0 = full).
- `net/server`: a ring of the last 32 `WorldState`s by snapshot id, reserved
  up front (no allocation per tick); per client, the newest acked id; one
  encode per client per snapshot.
- `net/client`: a ring of the last 32 rebuilt states; acks the newest in each
  Input; a delta whose baseline it no longer has is dropped (the server falls
  back to full once the client's ack is too old).

### Rules check

| Rule | Effect |
|---|---|
| 1. Only `simulate` changes the World | Unaffected: the client rebuilds the server's state as before, then predicts with `simulate`. |
| 2. Fixed step | Unaffected. |
| 3. No backend in core | Encoding is in `core/serialize`, pure bytes; sending is in `net`. |
| 4. No "down" in core | Unaffected. |
| 5. Server authority, plain-data World | `WorldState` is plain data. Delta input is untrusted: every slot index, group mask and baseline id is validated; a bad delta is dropped, never half-applied. |
| Code rules | No exceptions; reserved rings, so no allocation per tick after warm-up. |

### Tests (part A)

- `core.delta_round_trips`: for random sequences of world changes (spawns,
  destroys, moves, game data), `read_world_delta(baseline, write_world_delta(baseline, current))`
  gives the same bytes as `current`. Thousands of random cases, fixed seed.
- `core.delta_rejects_hostile_input`: truncated, random, wrong-baseline and
  out-of-range deltas are rejected without crashing (like the existing fuzz test).
- `net.deltas_survive_loss_and_reorder`: 20% loss + latency; the client's
  world still matches the server's after the movement stops.
- `net.full_snapshot_when_baseline_is_too_old`: a client silent for longer
  than 32 snapshots gets a full snapshot and recovers.
- `net.idle_scene_snapshots_are_tiny`: in `top-down` with no one moving, a
  steady-state snapshot is under 64 bytes.
- `no_allocation_in_hot_path` extended with delta encoding and decoding.
- `scraps_bench` prints full vs delta size per scene.

## Part B: relevance (needs an owner decision first)

Delta cannot help when everything moves. The next step is to stop sending
every entity to every player, and there are three common ways:

| Option | How | Cost / risk |
|---|---|---|
| **B1. Priority by distance** (recommended) | Every entity gains "priority" each tick: more when near the player, less when far. Each snapshot has a byte budget (e.g. 4 KB) and sends the highest-priority changed entities first; what is left out keeps its priority and goes next time. Near things update every tick, far things a few times a second. | Medium. Far entities on the client are slightly stale between updates; the client still predicts them, so they keep moving. Needs per-client tracking of what each client has, instead of one shared ring. |
| B2. Area of interest | Only entities within a radius of the player are sent at all. | Simpler to explain, but things pop in and out at the edge, and the client's world is incomplete, which complicates prediction (a far entity it cannot see may still matter to `simulate`). |
| B3. Lower snapshot rate | Snapshots at 20 or 30 Hz instead of 60 (already a server option: `--snapshot-interval`). | Trivial, cuts everything by 2 to 3 times, but other players' interpolation gets choppier and corrections arrive later. Can be combined with B1. |

Recommendation: do part A now, measure, and decide on B1 only if a real game
design needs hundreds of moving things online. For 5 to 10 players with tens
of entities, part A plus B3 is likely enough.

## Out of scope

Rounding positions to fewer bits, general-purpose compression (zlib) of
snapshots, delta-encoding the Input packets (already ~100 bytes), lag
compensation.

## Questions for the owner

1. Approve part A as written?
2. Part B: decide now (B1 / B2 / B3 / combination), or after part A is measured?
3. The snapshot window of 32 states (~0.5 s at 60 Hz): fine, or should a
   client be able to fall further behind before it costs a full snapshot?

## Results (part A, 2026-11-12)

The owner approved part A as written. Questions 2 and 3 were not answered,
so the spec's defaults apply: part B is decided after part A's measurements,
and the window is 32 snapshots (`kSnapshotWindow` in `net/protocol.h`).

Measured with `scraps_bench` (payload per snapshot, baseline 6 ticks old,
the player walking) and the net tests:

| Scene | Full | Delta (estimate) | Delta (measured) |
|---|---|---|---|
| isometric, players standing | 646 B | ~20 to 30 B | 35 B (`net.idle_scene_snapshots_are_tiny`) |
| isometric / side, player walking | 646 B | ~40 B | ~20 B average, 53 B largest |
| top-down | 1,319 B | ~40 B | ~140 B: its 8 critters never stop, which the estimate missed |
| crowd, 1,000 wanderers | 83,231 B | ~16 KB | ~14,100 B (17%), 15 datagrams |

Encoding the crowd delta takes ~0.06 ms, decoding ~0.02 ms (a full snapshot
~0.36 ms). Nothing allocates per tick after warm-up (`scraps_alloc_test`).

Differences from the text above, decided while implementing:

- The baseline snapshot id lives in the net payload (`u32 baseline_snapshot`),
  not inside the core delta, so `core/serialize` knows nothing about ids.
- The core delta starts with the baseline's **tick** (4 bytes) and is refused
  if it does not match, so a delta applied over the wrong baseline fails
  instead of rebuilding a plausible but wrong world. The current tick follows
  as a varint advance.
- The idle-size test uses the isometric scene, because top-down has critters
  that move by themselves; top-down is checked at under 256 bytes instead.
- `LoopbackHub` gained `set_jitter`, so the loss test also reorders datagrams.
- The protocol is version 3: older clients are refused with "versions differ".

Review (2026-11-12, `reviewer` agent): **Go**. Fixed after it:

- The server takes an explicit "I hold nothing" ack (0) at once, and accepts
  acks past 2^31 (`updated_snapshot_ack`, test `net.snapshot_acks_survive_reorder_and_wrap`).
- A delta whose baseline the client no longer has no longer discards a good
  snapshot staged earlier in the same poll.
- `read_world_delta` bounds new slots by 4 bytes each (was 1), and the test
  now reaches that check.
- `net.hot_reload_reaches_every_client` also checks that deltas resume after a reload.

Cost to know about: each side keeps 32 states reserved up front, about 6 MB
for the crowd scene and up to ~25 MB for the largest scene allowed; offline
play holds both rings in one process.
