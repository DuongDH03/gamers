#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/command.h"
#include "core/simulate.h"
#include "core/world.h"
#include "net/cookie.h"
#include "net/protocol.h"
#include "net/rate_limit.h"
#include "platform/net.h"

namespace scraps {

struct ServerConfig {
  int max_players = 10;                 // at most kMaxPlayers
  double client_timeout_seconds = 5.0;  // silence after which a client is dropped
  int snapshot_interval_ticks = 1;      // 1 = a snapshot every tick, 2 = every other tick, ...
  JobSystem* jobs = nullptr;            // optional worker threads for simulate (not owned)

  // Internet hardening (docs/specs/2026-11-12-hardening-and-look.md).
  // Apps fill it with random bits; tests use a fixed one. Left zero, the
  // server refuses every Hello (accepts_players() is false).
  CookieSecret cookie_secret;
  // Replies to Hellos, per sending IP address and for all of them together.
  RateLimit hello_replies_per_address{5.0, 10.0};
  RateLimit hello_replies_total{200.0, 200.0};
  // Datagrams accepted from one connected client: 8 per tick on average,
  // with room for a burst after a stall.
  RateLimit client_datagrams{8.0 * 60.0, 64.0};
};

// The authority (Rule 5). Owns the one true World and is the only place it is
// simulated. Runs the same way in the dedicated server executable (over UDP)
// and inside an offline client (over a LoopbackHub).
//
// Per frame the host calls poll() to read packets, then tick() once per fixed
// step. The server never reads the clock; time comes in through `now`.
//
// Joining is three steps. A first Hello gets only a small Challenge with a
// cookie; the server keeps nothing. A Hello that echoes a valid cookie
// reserves a slot and gets the Welcome. The player's entity is created, and
// snapshots start flowing, only once that address sends its first Input. A
// forged Hello (spoofed source address) therefore gets one reply smaller than
// itself and no slot, and replies to strangers are rate limited.
class Server {
 public:
  Server(Net& net, World world, SimulateFn simulate, std::string scene_name, std::uint32_t scene_hash,
         ServerConfig config = ServerConfig());

  // Handles every pending datagram and drops clients that went silent.
  void poll(double now);
  // Advances the world by one tick using the queued commands, then sends
  // snapshots when due.
  void tick();

  // Hot reload: replaces the world with one built from an edited scene.
  // The tick number carries on (ticks never go backwards), every connected
  // player re-joins the new world at the next tick, and every client gets a
  // new Welcome naming the new scene hash, which makes it reload too.
  void reload(World world, std::string scene_name, std::uint32_t scene_hash);

  // Game code hot reload: the next tick runs `simulate` (nullptr: the
  // engine's own). The world and every connection stay as they are. Call it
  // between ticks, never while one runs.
  void set_simulate(SimulateFn simulate) { m_simulate = simulate != nullptr ? simulate : &scraps::simulate; }

  const World& world() const { return m_world; }
  int client_count() const;  // confirmed players
  std::uint64_t dropped_commands() const { return m_dropped_commands; }
  std::uint64_t unsent_snapshots() const { return m_unsent_snapshots; }
  // Snapshots sent as deltas and as full states (each counted once per client).
  std::uint64_t delta_snapshots_sent() const { return m_delta_snapshots; }
  std::uint64_t full_snapshots_sent() const { return m_full_snapshots; }
  std::uint64_t challenges_sent() const { return m_challenges_sent; }
  // Hellos left unanswered because a rate limit was reached.
  std::uint64_t hellos_rate_limited() const { return m_hellos_rate_limited; }
  // Datagrams from connected clients dropped by their rate limit.
  std::uint64_t client_datagrams_dropped() const { return m_client_datagrams_dropped; }
  // Inputs and Byes from a player's address without that player's session
  // token: someone faking the address.
  std::uint64_t forged_datagrams() const { return m_forged_datagrams; }
  // False when ServerConfig::cookie_secret was left zero: such a server
  // answers no Hello at all, because its cookies could be forged.
  bool accepts_players() const { return is_set(m_config.cookie_secret); }
  // Slots in use, confirmed or not.
  int reserved_slots() const;
  // Payload size of the last snapshot sent to `player` (0 if none yet).
  std::size_t last_snapshot_size(PlayerId player) const;

 private:
  static constexpr std::size_t kInputQueueCapacity = 16;
  // With more queued frames than this, two are applied per tick to catch up.
  static constexpr std::size_t kInputQueueTarget = 3;
  // How long a slot reserved by a Hello waits for the client's first Input.
  static constexpr double kUnconfirmedTimeoutSeconds = 2.0;

  struct ClientSlot {
    bool connected = false;  // slot in use (reserved by a Hello)
    bool confirmed = false;  // sent at least one Input: a real player
    Address address;
    double last_heard = 0.0;
    std::uint32_t last_received = 0;  // newest input sequence queued
    std::uint32_t last_applied = 0;   // newest input sequence simulated (acked in snapshots)
    std::uint64_t session = 0;  // its session token (net/cookie.h)
    std::uint32_t acked_snapshot = kFullSnapshot;  // newest snapshot it holds: its delta baseline
    TokenBucket datagrams;  // its rate limit
    std::size_t last_snapshot_size = 0;
    std::array<InputFrame, kInputQueueCapacity> queue{};
    std::size_t queue_head = 0;
    std::size_t queue_count = 0;
  };

  void handle_datagram(const Address& from, const std::uint8_t* data, std::size_t size, double now);
  void handle_hello(const Address& from, ByteReader& reader, double now);
  bool may_answer_stranger(const Address& to, double now);
  bool cookie_valid(const Address& from, std::uint64_t cookie, double now) const;
  void send_challenge(const Address& to, double now);
  void handle_input(int slot, ByteReader& reader);
  void disconnect(int slot);
  void send_welcome(int slot);
  void send_snapshots();
  void send_snapshot(ClientSlot& client, const std::vector<std::uint8_t>& payload);
  // The kept state with this id, or nullptr if it is not (or no longer) kept.
  const WorldState* baseline(std::uint32_t snapshot_id) const;
  void forget_snapshots();
  int find_client(const Address& address) const;
  void push_command(const Command& command);

  Net& m_net;
  World m_world;
  SimulateFn m_simulate;
  std::string m_scene_name;
  std::uint32_t m_scene_hash;
  ServerConfig m_config;

  std::array<ClientSlot, kMaxPlayers> m_clients{};
  // Join and leave requests per slot, applied at the next tick (leave first).
  // Flags rather than a queue: they can never overflow, however a client
  // behaves, so a join or leave is never lost.
  std::array<bool, kMaxPlayers> m_pending_join{};
  std::array<bool, kMaxPlayers> m_pending_leave{};
  CommandList m_tick_commands;
  InputMessage m_input_message;

  std::vector<std::uint8_t> m_receive_buffer;
  std::vector<std::uint8_t> m_send_buffer;
  std::vector<std::uint8_t> m_full_bytes;   // this snapshot as a full state, built when first needed
  std::vector<std::uint8_t> m_delta_bytes;  // this snapshot for one client, as a delta
  // The states of the last kSnapshotWindow snapshots, at index id % window,
  // reserved up front so keeping them allocates nothing per tick.
  struct SentState {
    std::uint32_t id = kFullSnapshot;
    WorldState state;
  };
  std::array<SentState, kSnapshotWindow> m_sent{};
  std::uint32_t m_snapshot_id = 0;
  int m_ticks_since_snapshot = 0;
  std::uint64_t m_dropped_commands = 0;
  std::uint64_t m_unsent_snapshots = 0;
  std::uint64_t m_delta_snapshots = 0;
  std::uint64_t m_full_snapshots = 0;
  AddressRateLimiter m_hello_limiter;
  TokenBucket m_hello_total;
  std::uint64_t m_challenges_sent = 0;
  std::uint64_t m_hellos_rate_limited = 0;
  std::uint64_t m_client_datagrams_dropped = 0;
  std::uint64_t m_forged_datagrams = 0;
};

}  // namespace scraps
