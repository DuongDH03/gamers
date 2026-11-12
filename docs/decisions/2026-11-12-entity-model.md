# Entity model: array of structs

- Date: 2026-11-12
- Status: **Accepted** (owner, 2026-11-12)

## Problem

How entities and their components are stored in `World`. Rule 5 requires
`World` to be plain, copyable, serializable data with integer handles.

## Options

1. **Array of structs.** One `Entity` struct holds every component field
   (position, velocity, size, color, controller, game data). `flags` say which
   behaviours apply. Entities live in one `std::vector<Entity>`; dead slots go
   on a free list. Handles are `{index, generation}`.
2. **Sparse-set ECS.** One packed array per component type plus a registry,
   as EnTT and big engines do.

## Decision

**Array of structs.**

- Easiest to read and debug: one entity is one struct you can print.
- Copying a `World` is a `std::vector` copy of trivially copyable structs
  (about 2 µs for 1,000 entities); serialization is a simple loop.
- Fast enough for this engine's scale (thousands of entities, not millions).

Costs we accept: every entity pays for every field (about 100 bytes each);
games cannot add new component types, only use the four `user` integers and
`kind`. If that becomes limiting, the upgrade path is to move fields into
per-component arrays behind the same `EntityId`, one component at a time.
