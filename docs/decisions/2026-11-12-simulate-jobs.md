# Passing the job system to `simulate`

- Date: 2026-11-12
- Status: **Accepted** (owner, 2026-11-12). Rule 1's example signature in the
  constitution was updated to match, on the owner's instruction.

## Problem

Stage 8 runs movement on worker threads. The worker threads live in a
`JobSystem` object, and `simulate` needs to reach it. Rule 1 shows the
signature `void simulate(World&, const CommandList&, Tick)`.

The places it could come from:

- **inside `World`**: forbidden; World is plain data with no pointers (Rule 5);
- **a global or singleton**: forbidden by the code rules;
- **a parameter**: the only option left.

## Options

1. **An optional fourth parameter**:
   `simulate(World&, const CommandList&, Tick, JobSystem* jobs = nullptr)`.
   Calls written the Rule 1 way still compile and mean the same thing. The
   parameter says *how* to run (which threads), never *what* changes. Games'
   `SimulateFn` gains the same parameter and passes it on.
2. **A separate function**, `simulate_parallel(...)`. That gives two names for
   the one mutator, and games would have to write two versions.
3. **No threads inside `simulate`**: the host runs `simulate` itself in
   parallel. This doesn't work; one tick of one world can't be split from
   outside.

## Decision

**Option 1**, implemented. Guarantees, each checked by a test:

- Results are identical with and without threads, byte for byte
  (`core.threaded_simulate_matches_single_threaded`).
- No allocation per tick (`no_allocation_in_hot_path` runs threaded).
- Only `simulate` still changes the World: the job system just runs the parts
  of `simulate` that are safe to split, each entity writing only itself.

Rule 1's example signature now reads
`void simulate(World& world, const CommandList& commands, Tick tick, JobSystem* jobs = nullptr);`.
