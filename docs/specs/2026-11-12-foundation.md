# Spec: foundation (stages 1 to 6, plus the netcode core)

- Date: 2026-11-12
- Status: **Implemented.** Scope approved by the owner on 2026-11-12
  ("Stages 1-6 + net core").
- Decisions used: [engine name](../decisions/2026-11-12-engine-name.md),
  [entity model](../decisions/2026-11-12-entity-model.md),
  [level model](../decisions/2026-11-12-level-model.md),
  [test framework](../decisions/2026-11-12-test-framework.md),
  [net backends](../decisions/2026-11-12-net-backends.md).

## Goal

A running engine with every one of the five rules in place from the start, so
later stages add features without moving foundations. The architecture is
explained in [architecture.md](../architecture.md); this spec lists what was
promised and how each promise is checked.

## Interfaces

| Interface | File | Notes |
|---|---|---|
| `void simulate(World&, const CommandList&, Tick)` | `core/simulate.h` | Rule 1. Games pass their own `SimulateFn` that calls this one. |
| `FixedStepClock::advance(frame_seconds) -> ticks`, `alpha()` | `core/fixed_step.h` | Rule 2. 60 Hz, max 8 ticks per frame. |
| `Window`, `Input`, `Renderer2D`, `Audio`, `Net` | `platform/*.h` | Rule 3. Implemented by `sdl3`, `headless`, `udp`, `LoopbackNet`. |
| `View`: `movement_rules`, `world_to_screen`, `screen_to_ground_direction`, `draw` | `views/view.h` | Rule 4. Three implementations. |
| `Server(net, world, simulate, scene name, hash)`, `Client(net, server address, simulate, config)` | `net/*.h` | Rule 5. Same code online and offline. |
| `parse_scene`, `load_scene`, `make_world` | `core/scene.h` | Text scene files. |
| `write_world` / `read_world`, `write_world_state` / `read_world_state` | `core/serialize.h` | Little-endian, validated, never reads out of bounds. |

## Acceptance (all automated in `ctest`)

| Promise | Check |
|---|---|
| 60 ticks/s at any frame rate | `core.fixed_step_keeps_60_ticks_per_second` |
| A stall does not freeze the game | `core.fixed_step_drops_backlog_after_a_stall` |
| Stale entity handles never resolve | `core.stale_handles_stop_resolving` |
| `World` copies are independent | `core.world_copies_are_independent` |
| Walls block, entities slide along walls | `core.walls_stop_movement_and_allow_sliding` |
| Gravity, landing, jumping only from the ground | `core.gravity_lands_and_jumps_only_from_the_ground` |
| Side view locks y | `core.side_rules_lock_the_y_axis` |
| `World` round-trips through bytes; garbage is rejected without crashing | `core.world_round_trips_through_bytes`, `core.truncated_or_random_bytes_are_rejected_safely` |
| Scene errors name the line and problem | `scene.errors_name_the_line_and_the_problem` |
| Isometric projection looks along (1,1,1); input matches the screen in all views | `views.isometric_looks_along_the_diagonal`, `views.input_directions_match_the_screen` |
| Isometric draws back to front, entities in front of their floor | `views.isometric_paints_back_to_front`, `views.isometric_entities_stand_in_front_of_their_floor` |
| Every view draws every scene in one batch | `views.every_view_draws_every_bundled_scene` |
| Key taps are never lost or doubled | `views.controls_turn_keys_into_commands` |
| Two clients share a world; inputs move the server's player | `net.two_clients_share_one_world`, `net.inputs_move_the_authoritative_player` |
| Prediction hides 100 ms latency; client and server converge | `net.prediction_hides_latency` |
| 20% packet loss still converges | `net.lossy_network_still_converges` |
| Server full, wrong version, different scene file are refused | `net.a_full_server_rejects`, `net.wrong_protocol_version_is_rejected`, `net.a_different_scene_file_is_refused` |
| Timeouts and Bye remove players | `net.silence_times_out_both_ways`, `net.bye_removes_the_player` |
| Clients cannot act for other players or send server-only commands | `net.clients_cannot_speak_for_others` |
| A forged Hello gets no entity, no snapshot stream, and its slot back | `net.a_forged_hello_gets_no_player_and_no_snapshots` |
| Join/leave spam never leaves ghosts or loses a player | `net.reconnect_spam_never_loses_a_join_or_leave` |
| Hostile snapshot values (huge sizes, positions, duplicate free slots) are rejected | `core.hostile_snapshot_values_are_rejected` |
| Entities that fly out of the world are removed | `core.entities_that_leave_the_world_are_removed` |
| Real UDP works | `udp.a_client_joins_over_real_udp` |
| A game adds rules and commands without engine changes | `sandbox.coins_are_picked_up_and_dropped` |
| No allocation in simulate / world copy / draw after warm-up | `no_allocation_in_hot_path` |
| Dependencies point down only | `layer_rules` |
| Each executable runs end to end | `client_offline_*`, `server_runs`, `bench_runs` |

Checked by hand on 2026-11-12: the SDL3 window in all three views; one
`scraps_server` with two `scraps_client --connect` processes over UDP; and
the 1,001-entity crowd scene (82 datagrams per snapshot) over UDP.

An independent review on 2026-11-12 found and led to fixes for: the
unauthenticated join (snapshot reflection), lost join/leave commands under
spam, unbounded snapshot values, one replay per snapshot instead of per poll,
lost capacity in the client's predicted world, a shared command list one
client could fill, duplicate free slots, oversized scenes, and small UDP
buffers.

## Known limits

- Remote players are extrapolated with their last input, not interpolated
  between snapshots.
- Snapshots are full state every tick (83 KB for 1,000 entities); fine on a
  LAN, too much for the internet at that size.
- Entities do not collide with each other; isometric sorting can misorder an
  entity that straddles cells next to a tall block.
- One scene per server run; no hot reload yet.
