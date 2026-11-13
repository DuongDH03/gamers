#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/command.h"
#include "core/simulate.h"
#include "core/world.h"
#include "net/protocol.h"
#include "platform/net.h"

namespace scraps {

enum class ClientState : std::uint8_t {
  kConnecting,    // sending Hello, waiting for Welcome
  kConnected,     // playing
  kRejected,      // the server said no (see error())
  kFailed,        // could not load the server's scene (see error())
  kTimedOut,      // the server went silent
  kDisconnected,  // disconnect() was called
};

const char* client_state_name(ClientState state);

struct ClientConfig {
  std::string scene_directory;           // where "<scene>.scene" files are
  double hello_interval_seconds = 0.25;  // Hello resend period while connecting
  double timeout_seconds = 5.0;          // silence after which the server is presumed gone
  // How far in the past other players are drawn, in ticks. It must cover at
  // least two snapshot intervals plus jitter, or they freeze between snapshots.
  int interpolation_delay_ticks = 4;
  JobSystem* jobs = nullptr;  // optional worker threads for prediction (not owned)
};

// The player's side of the connection (Rule 5). Sends this player's commands
// to the server and shows a predicted world.
//
// Prediction and reconciliation, in short:
//   every tick:      remember (sequence, commands); send them; simulate locally
//   every snapshot:  world = server state; re-simulate every remembered input
//                    the server has not applied yet (sequence > ack)
// So the local player moves the moment a key is pressed, and any
// disagreement with the server is corrected at the next snapshot.
//
// Other players are different: their inputs are unknown here, so predicting
// them means guessing. display_world() draws them a few ticks in the past,
// between two real snapshots, instead (interpolation).
class Client {
 public:
  Client(Net& net, Address server, SimulateFn simulate, ClientConfig config);

  // Reads pending datagrams (Welcome, snapshots), resends Hello while
  // connecting and detects timeouts. Call every frame.
  void poll(double now);
  // One fixed step: records and sends `commands`, then predicts the world one
  // tick ahead. Does nothing until connected.
  void tick(const CommandList& commands, double now);
  // Tells the server we are leaving.
  void disconnect();

  // Pause (docs/specs/2026-11-14-offline-essentials-and-ui.md, part 4),
  // offline only: the app pauses the embedded server too. While paused,
  // tick() does nothing (no prediction, no inputs sent) and the server never
  // times out, however long the pause. poll() still reads datagrams.
  void set_paused(bool paused);
  bool paused() const { return m_paused; }

  // Game code hot reload: prediction runs `simulate` from the next tick on
  // (nullptr: the engine's own). The predicted world and the input history
  // stay. Call it between ticks, never while one runs.
  void set_simulate(SimulateFn simulate) { m_simulate = simulate != nullptr ? simulate : &scraps::simulate; }

  ClientState state() const { return m_state; }
  const std::string& error() const { return m_error; }
  PlayerId player_id() const { return m_player; }
  bool has_world() const { return m_has_world; }

  // The predicted world one tick ago and now.
  const World& previous_world() const { return m_previous; }
  const World& current_world() const { return m_current; }
  EntityId local_entity() const { return player_entity(m_current, m_player); }

  // The world as it should be drawn this frame (`alpha` from the loop's
  // FixedStepClock): predicted entities blended between the last two ticks,
  // other players interpolated between snapshots. Draw it with alpha 0.
  // A render-only copy: nothing simulates it (Rule 1).
  const World& display_world(float alpha);

  const std::string& scene_name() const { return m_scene_name; }
  // Goes up by one every time a new world is loaded (first Welcome, and each
  // hot reload), so an app knows when to reload textures.
  std::uint32_t world_generation() const { return m_world_generation; }
  double round_trip_seconds() const { return m_round_trip; }
  std::uint32_t unacknowledged_inputs() const { return m_next_sequence - 1 - m_last_acked; }
  // Snapshots rebuilt from a full state and from a delta; deltas dropped
  // because their baseline was no longer kept here.
  std::uint64_t full_snapshots_received() const { return m_full_snapshots; }
  std::uint64_t delta_snapshots_received() const { return m_delta_snapshots; }
  std::uint64_t deltas_without_baseline() const { return m_missing_baselines; }

 private:
  static constexpr std::size_t kHistorySize = 128;  // ~2 s of inputs at 60 Hz
  static constexpr std::size_t kSampleCount = 16;   // snapshots kept for interpolation

  struct HistoryEntry {
    InputFrame frame;
    double sent_at = 0.0;
  };

  // Where every player was in one snapshot.
  struct PlayerSample {
    EntityId id;
    Vec3 position;
  };
  struct SnapshotSample {
    Tick tick = 0;
    std::array<PlayerSample, kMaxPlayers> players{};
  };

  void handle_datagram(const std::uint8_t* data, std::size_t size, double now);
  void handle_welcome(ByteReader& reader, double now);
  void handle_fragment(ByteReader& reader, double now);
  bool load_world(const WelcomeMessage& welcome);
  void reconcile(double now);
  void send_inputs();
  void send_hello(double now);
  void record_sample();
  // kReplaced: read, and the server's world was replaced since (a new epoch).
  enum class SnapshotRead : std::uint8_t { kOk, kReplaced, kNoBaseline, kCorrupt };
  SnapshotRead read_snapshot(const std::uint8_t* data, std::size_t size);
  void keep_snapshot(std::uint32_t snapshot_id);
  bool sample_position(EntityId id, double tick, Vec3& out) const;

  Net& m_net;
  Address m_server;
  SimulateFn m_simulate;
  ClientConfig m_config;

  ClientState m_state = ClientState::kConnecting;
  std::string m_error;
  PlayerId m_player = kNoPlayer;
  std::string m_scene_name;
  std::uint32_t m_scene_hash = 0;
  std::uint32_t m_world_generation = 0;

  std::uint64_t m_cookie = 0;   // from the server's Challenge; echoed in Hello
  std::uint64_t m_session = 0;  // from the Welcome; starts every Input and Bye
  bool m_hello_pending = false;  // we sent a Hello of our own: one Challenge may be answered
  bool m_started = false;
  double m_last_hello = 0.0;
  double m_last_heard = 0.0;

  bool m_has_world = false;
  World m_authoritative;  // last server state (staging copy, keeps the level)
  World m_current;        // predicted
  World m_previous;       // predicted, one tick earlier
  World m_display;        // what display_world() returns

  std::vector<HistoryEntry> m_history;  // ring buffer indexed by sequence % kHistorySize
  std::uint32_t m_next_sequence = 1;
  std::uint32_t m_last_acked = 0;
  double m_round_trip = 0.0;

  std::array<SnapshotSample, kSampleCount> m_samples{};  // ring buffer, oldest first from m_sample_next
  std::size_t m_sample_next = 0;
  std::size_t m_sample_count = 0;
  double m_interpolation_tick = 0.0;  // the server tick other players are drawn at

  SnapshotAssembler m_assembler;
  // The states of the last kSnapshotWindow snapshots, as rebuilt here, at
  // index id % window: the baselines the server may send deltas against.
  struct KeptState {
    std::uint32_t id = kFullSnapshot;
    WorldState state;
  };
  std::array<KeptState, kSnapshotWindow> m_kept{};
  std::uint32_t m_acked_snapshot = kFullSnapshot;  // newest snapshot rebuilt; sent in every Input
  std::uint64_t m_full_snapshots = 0;
  std::uint64_t m_delta_snapshots = 0;
  std::uint64_t m_missing_baselines = 0;
  bool m_snapshot_pending = false;  // m_authoritative holds a snapshot not yet reconciled
  // The server replaced its world (a loaded save, at an earlier or a later
  // tick): the next reconcile starts prediction over instead of blending.
  bool m_world_replaced = false;
  // The server's world epoch from the last full snapshot (net/protocol.h);
  // m_has_epoch is false until the first one after a Welcome.
  std::uint32_t m_world_epoch = 0;
  bool m_has_epoch = false;
  bool m_paused = false;
  bool m_restart_timeout = false;  // set on resume: the paused time never counts as silence
  std::uint32_t m_snapshot_ack = 0;
  std::vector<std::uint8_t> m_receive_buffer;
  std::vector<std::uint8_t> m_send_buffer;
};

}  // namespace scraps
