# Test framework: a small in-house harness + CTest

- Date: 2026-11-12
- Status: **Accepted** (owner, 2026-11-12)

## Problem

The constitution leaves the test framework open. Tests were needed from the
first commit: the netcode and the isometric draw order are too easy to break
silently.

## Options

1. **In-house harness** (`tests/harness.h`, ~70 lines): `CHECK`, `CHECK_NEAR`,
   and a `Registry` that each test file fills by hand. CTest runs groups.
2. **doctest or Catch2**: richer output and matchers, but a downloaded
   dependency and macro-heavy auto-registration (static initializers, which
   are global state).
3. **GoogleTest**: the industry standard; heavier still.

## Decision

**Option 1.** It fits "write the core ourselves" (constitution section 1),
downloads nothing, uses no global state, and has everything the current tests
need. Switching later is mechanical: each test is a plain function taking a
`Context&`.

Exception: `tests/alloc_test.cpp` must replace global `operator new` to count
allocations, which needs one global counter. It is its own executable so the
counter cannot affect other tests.
