---
name: review-checklist
description: The checklist a change to the Scraps engine must pass before it is accepted, ending in a Go or No-Go verdict. Use it when reviewing a diff, a pull request, or your own finished work in this repository, and whenever someone asks "is this ready", "review this", or "can we merge".
---

# Review checklist

Read the engine constitution first. Then check the change against every item
below. Each item says what to look for, not just a rule name. Cite file and
line for every problem.

## Rules

- [ ] **R1, simulate is the only mutator.** No code outside `simulate` (and the
  functions it calls) writes to a running `World`. Look for non-const
  `World&` or `Entity*` in input, UI, net, views and apps code. Building the
  first world (`make_world`) and test setup are allowed.
- [ ] **R1, game features are commands.** New player actions add a
  `CommandType` (game ones via `game_command(n)`) handled in a simulate, not
  direct calls.
- [ ] **R2, time.** Simulation code uses `kTickSeconds` / a `dt` or `Tick`
  argument. Nothing outside the loops in `apps/` reads a clock
  (`std::chrono`, `SDL_GetTicks`, ...). Rendering interpolates with `alpha`.
- [ ] **R3, layers.** `ctest -R layer_rules` passes. No SDL, Winsock or other
  library header outside its backend. A new platform need is a new interface
  method with a headless implementation too, not a backend call.
- [ ] **R4, no "down" in core.** Core code does not assume a gravity
  direction, a draw order or a fixed z. Anything perspective-specific is in a
  `View` or in `MovementRules`.
- [ ] **R5, World is plain data.** New `World`/`Entity`/`Level` fields are
  values: no pointers, `std::function`, `std::string` or owning containers of
  non-trivial types. Every new field is written and read in
  `core/serialize.cpp`, with validation on read.
- [ ] **R5, server authority.** The server never trusts client data: player ids
  are rewritten, server-only commands are dropped, sizes and counts are
  bounded, floats are checked for NaN.

## Code rules

- [ ] Builds with zero warnings on GCC **and** MSVC.
- [ ] No exceptions in `engine/`. No raw `new`/`delete`. No globals or singletons.
- [ ] No allocation in simulate or draw after warm-up (`no_allocation_in_hot_path`
  passes; new per-frame buffers are reused members, not locals).
- [ ] Naming: `PascalCase` types, `snake_case` functions/variables, `kConstant`,
  `m_member` for private members.
- [ ] Code run through `JobSystem::parallel_for` writes only data owned by its
  own index range and reads only data nobody writes during the call; anything
  touching shared lists (free list, player table) runs after, on one thread.
  Threaded and single-threaded results stay byte-identical.
- [ ] Image paths in scenes go through `is_valid_asset_path`; art problems
  degrade to flat colours, never stop the game.
- [ ] Untrusted input (network bytes, scene files) goes through `ByteReader` or
  the scene parser and cannot cause out-of-bounds access, huge allocations or
  float-to-int overflow.

## Tests and docs

- [ ] New behaviour has a test that would fail without the change.
- [ ] `ctest` passes on both compilers.
- [ ] A new decision has a dated file in `docs/decisions/`; a stage change
  updates `docs/plan.md`; a new scene directive updates `docs/scene-format.md`.
- [ ] Comments explain why, not what, and match the code.

## Verdict

End the review with exactly one of:

- **Go**: every item passes, or the only findings are optional suggestions.
- **No-Go**: list the blocking findings, most serious first, each with file,
  line, what is wrong, and what would fix it.

A rule conflict the change cannot avoid is not the reviewer's to waive: mark
it No-Go and say the owner must decide (constitution section 8).
