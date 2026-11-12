# Rust: a learning plan for later

- Date: 2026-11-12
- Status: **Future plan**, starts after stage 8 (job system) is done. The
  engine stays C++17 (constitution section 1). Nothing here changes that
  unless the owner records a decision in `docs/decisions/`.
- Written for someone with **no Rust experience**, who knows the Scraps C++
  code.

## Why learn Rust, and why after stage 8

Rust is a systems language like C++: compiled to machine code, no garbage
collector, the same speed. The difference is that the compiler checks, before
the program runs, that memory and threads are used safely. Many bugs that C++
finds only by testing or code review (or never) are compile errors in Rust.

The order matters for learning. In C++ you will have *seen* the bugs:

- In the 2026-11-12 review, the reviewer checked whether any code kept an
  `Entity*` while `spawn_entity` could grow the vector (a dangling pointer).
- In stage 8, you will write a race condition on purpose and watch a test fail.

Then Rust shows you how a compiler can rule those bugs out. Learning Rust
first would hide them: in normal Rust you cannot even write that race.

## Part 0. Setup on this machine (about 30 minutes)

1. Install **rustup** from <https://rustup.rs> (the official installer). It
   installs `rustc` (the compiler), `cargo` (build tool and package manager)
   and the standard library.
2. Choose the default **MSVC toolchain** (`x86_64-pc-windows-msvc`). Visual
   Studio Build Tools 2022 is already installed here, and Rust uses its linker.
3. Check that it worked:

   ```bash
   cargo --version
   ```

4. Editor: install the **rust-analyzer** extension (VS Code or similar). It
   shows types and borrow errors as you type, which is most of how people
   learn Rust.
5. First project:

   ```bash
   cargo new hello_rust
   ```

   Then `cargo run` inside it. `Cargo.toml` is the project file (like
   `CMakeLists.txt` plus dependencies). `src/main.rs` is the code.

## Part 1. The language, mapped to what you know (2 to 4 weeks, evenings)

Main resource: **The Rust Programming Language** ("the Book"), free at
<https://doc.rust-lang.org/book/>. Practice alongside it: **Rustlings**
(small exercises, <https://github.com/rust-lang/rustlings>) and
**Rust by Example** (<https://doc.rust-lang.org/rust-by-example/>).

Read in this order. Each row says what the concept is, and where the same idea
already lives in Scraps.

| # | Rust concept | Book chapter | What it means | The Scraps C++ equivalent |
|---|---|---|---|---|
| 1 | `cargo`, crates, modules | 1, 7, 14 | Build tool, packages, files as modules | CMake targets, `engine/core` as a library, `#include` |
| 2 | Variables are immutable by default; `let mut` | 3 | You must say when something can change | `const` everywhere, the other way around |
| 3 | **Ownership** | 4 | Every value has exactly one owner; when the owner goes away, the value is freed | `std::unique_ptr`, `std::vector` owning its elements, RAII (`UdpNet`'s destructor closes the socket) |
| 4 | **Borrowing**: `&T` and `&mut T` | 4 | Many readers *or* one writer, never both at once | `const World&` vs `World&`. Rust **enforces** what we only follow by convention, e.g. Rule 1: only `simulate` gets a `&mut World` |
| 5 | Structs, `impl` blocks | 5 | Data plus methods | `struct World`, `class Server` |
| 6 | **Enums with data** and `match` | 6 | A value that is one of several shapes; `match` must handle every shape | `CommandType` + `Command` (Rust: `enum Command { Move(Vec2), Jump, Join(PlayerId) }`), and `switch` with `default:` |
| 7 | `Option<T>`, `Result<T, E>` | 6, 9 | "Maybe a value" and "a value or an error" as types; no null, no exceptions | `find_entity` returning `nullptr`; `load_scene(..., std::string& error)` returning `bool`. Our "no exceptions in core" rule is how Rust always works |
| 8 | `Vec<T>`, `String`, `HashMap` | 8 | Standard containers | `std::vector`, `std::string`, `std::unordered_map` |
| 9 | Traits | 10 | Shared behaviour, like interfaces | `class Renderer2D` with virtual functions; `trait Renderer2D { fn fill_triangles(&mut self, ...); }` |
| 10 | Generics | 10 | Code for many types | Templates |
| 11 | **Lifetimes** (`'a`) | 10 | The compiler proves a borrowed reference never outlives what it points to | Exactly the dangling `Entity*` question from the review, and why `spawn_entity` takes `Vec3` by value |
| 12 | Closures, iterators | 13 | `entities.iter().filter(|e| e.alive)` | Lambdas, range-for loops |
| 13 | `Box`, `Rc`, `RefCell` | 15 | Heap ownership, shared ownership, runtime-checked borrows | `unique_ptr`, `shared_ptr`. Games avoid `Rc<RefCell<>>` webs, which is why we use `EntityId` handles |
| 14 | **Threads**: `Send`, `Sync`, `Arc`, `Mutex`, atomics | 16 | The compiler refuses to share data between threads unless it is safe | Stage 8. `count = count + 1` from two threads does not compile in Rust; you must use `AtomicU32` or a `Mutex` |
| 15 | `unsafe` | 19 | A block where *you* promise the rules hold (raw pointers, calling C) | Everything in C++ is "unsafe" in this sense |
| 16 | FFI (`extern "C"`) | 19 | Calling C code and being called from C | How Rust would talk to our C++ engine |

**What usually hurts at first:** the borrow checker. Example that compiles in
C++ but not Rust:

```rust
let player = &mut world.entities[0];      // borrow one element mutably...
world.entities.push(Entity::default());   // ...then grow the vector: ERROR
player.position.x += 1.0;                 // (player could now dangle)
```

That is the exact bug class the reviewer looked for in `drop_coin`. Rust's
answer is the one we already chose: refer to entities by index or handle, not
by pointer.

**Done when:** you can finish Rustlings, and explain in your own words why
the example above is rejected.

## Part 2. First real exercise: port movement and collision (1 to 2 weeks)

Rewrite a small, well-tested piece of Scraps in Rust, as a separate project
outside the engine (`experiments/rust-movement/`, its own `Cargo.toml`).

1. Port the data: `Vec3`, `Entity` (only the fields movement uses), `Level`
   with its `TileGrid`, `MovementRules`.
2. Port `is_solid_cell`, `box_overlaps_solid` and `step_movement` from
   `engine/core/movement.cpp`.
3. Port the core tests from `tests/test_core.cpp` (walls, sliding, gravity,
   jump, pit) using Rust's built-in test runner: `#[test] fn walls_stop_movement() { ... }`,
   run with `cargo test`.
4. Benchmark it against `scraps_bench`'s simulate number with a simple
   timing loop, and in release mode (`cargo run --release`).

What to notice and write down:

- Where the borrow checker made you restructure code, and whether the
  C++ version had a hidden risk at that spot.
- `f32` maths and `as i32` conversions: Rust's float-to-int cast *saturates*
  instead of being undefined behaviour, so our `clamp_coordinate` guard
  becomes unnecessary.
- How `Result` replaced the `bool` + error-string pattern.

**Done when:** all ported tests pass, and there is a short note comparing the
two versions (lines of code, speed, which bugs were impossible).

## Part 3. The threading lesson again, in Rust (a few evenings)

Repeat the stage 8 exercise:

1. Try to write `count = count + 1` from several threads on a shared `u32`.
   Read the compiler error and explain it (hint: `Send`, `Sync`).
2. Fix it three ways: `Mutex<u32>`, `AtomicU32::fetch_add`, and giving each
   thread its own counter then summing (no sharing at all).
3. Port the job system's "update entities in parallel" with **rayon**
   (`entities.par_iter_mut()`), a crate that splits work across cores safely.
   Compare with our hand-written job system.

**Done when:** you can say why the C++ race was possible and the Rust one is
not, in terms of `&mut` (one writer) and `Sync`.

## Part 4. A tiny game with a Rust engine (1 to 2 weeks, optional)

Make a small top-down game in **Bevy** (<https://bevyengine.org>), the most
used Rust engine.

- Bevy is a full **ECS** (entity component system). Compare it with our
  array-of-structs decision (`docs/decisions/2026-11-12-entity-model.md`):
  where did each feel easier?
- Bevy runs systems on many threads automatically, because it knows from the
  function signatures which systems read and which write each component.
  That is Rule 1 plus stage 8 enforced by types.

**Done when:** a player moves, collides and collects something, and you have
notes on ECS vs our model.

## Part 5. Decision point: should Rust enter Scraps?

Only after parts 1 to 3. The owner writes `docs/decisions/YYYY-MM-DD-rust.md`
choosing one of:

| Option | What it means | Cost |
|---|---|---|
| **A. Stay C++ only** | Rust stays a learning exercise | None |
| **B. Rust UI shell (Tauri)** | React UI in a Tauri app; the C++ engine as a library behind a plain C interface (`extern "C"` functions), called from Rust | Two languages and a C bridge; ties into the open UI decision in `docs/plan.md` |
| **C. Rust tools** | Level editor, asset packer, or a test bot in Rust; the engine stays C++ | Small; tools talk to the engine through files or the network protocol, which needs no bridge |
| **D. Port the engine** | Rewrite Scraps in Rust | Large. The architecture ports cleanly (plain-data World, handles, commands are Rust-friendly), but it is a new project |

If B or D is chosen, it is a new decision record, and for D a constitution
change (section 1 says C++17). Only the owner changes that.

### If option B is chosen: how C++ and Rust would connect

```
React (UI)  ⇄  Tauri (Rust)  ⇄  extern "C" API  ⇄  Scraps (C++ static library)
```

- Scraps exposes a small C interface, e.g. `scraps_client_create`,
  `scraps_client_tick`, `scraps_send_command`, `scraps_get_state`. C, not
  C++, because C is the one interface every language can call.
- Rust declares those functions in an `extern "C"` block, wraps them in a
  safe Rust type, and builds the C++ library from `build.rs` with the `cc`
  crate. **cbindgen** or **bindgen** can generate the declarations, and the
  **cxx** crate is an alternative that bridges C++ more directly.
- Per-frame graphics data should not go through Tauri's JSON messages;
  see the UI decision notes in `docs/plan.md`.

## Resources

| What | Where |
|---|---|
| The Book | <https://doc.rust-lang.org/book/> |
| Rustlings exercises | <https://github.com/rust-lang/rustlings> |
| Rust by Example | <https://doc.rust-lang.org/rust-by-example/> |
| Standard library docs | <https://doc.rust-lang.org/std/> |
| Threads and atomics, in depth | *Rust Atomics and Locks* by Mara Bos (free online) |
| Game-specific | <https://arewegameyet.rs> (the Rust game ecosystem), the Bevy book |
| Rust for C++ programmers | <https://github.com/nrc/r4cppp> |
