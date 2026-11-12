# Which backends implement `Net`

- Date: 2026-11-12
- Status: **Accepted** (owner, 2026-11-12). No rule changes: Rule 3 is unchanged.

## Problem

Rule 3 names two backends, `sdl3` and `headless`, and lists `Net` among the
platform interfaces. But SDL3 has no networking, and the dedicated server is
headless yet needs real sockets. Offline play (Rule 5) needs a network that
never leaves the process.

## Decision

`Net` is a datagram interface (UDP semantics) with two implementations, chosen
in `apps/` like every backend:

| Implementation | Where | Used by |
|---|---|---|
| `scraps::udp::UdpNet` | `engine/backends/udp` | `scraps_server`, `scraps_client --connect` |
| `scraps::LoopbackNet` + `LoopbackHub` | `engine/net/loopback` | offline client (embedded server), tests |

`sdl3` and `headless` implement `Window`, `Input`, `Renderer2D` and `Audio`.
So "headless" means "no window, no drawing, no sound", and the server is
headless + udp.

`UdpNet` uses the OS socket API directly (Winsock / BSD sockets), which keeps
the protocol code ours, per "libraries only at the edges". The loopback lives
in `engine/net` because it is pure engine code with no OS dependency; it can
add latency and packet loss, which the netcode tests rely on.
