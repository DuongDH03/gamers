#include "net/client.h"

#include <cmath>
#include <initializer_list>
#include <utility>

#include "core/scene.h"

namespace scraps {
namespace {

constexpr int kMaxDatagramsPerPoll = 4096;
// If the interpolation clock is further than this from where it should be
// (after a stall or a reload), it jumps instead of drifting back.
constexpr double kInterpolationSnapTicks = 10.0;

}  // namespace

const char* client_state_name(ClientState state) {
  switch (state) {
    case ClientState::kConnecting: return "connecting";
    case ClientState::kConnected: return "connected";
    case ClientState::kRejected: return "rejected";
    case ClientState::kFailed: return "failed";
    case ClientState::kTimedOut: return "timed out";
    case ClientState::kDisconnected: return "disconnected";
    default: return "unknown";
  }
}

Client::Client(Net& net, Address server, SimulateFn simulate, ClientConfig config)
    : m_net(net),
      m_server(server),
      m_simulate(simulate != nullptr ? simulate : &scraps::simulate),
      m_config(std::move(config)) {
  if (m_config.interpolation_delay_ticks < 1) m_config.interpolation_delay_ticks = 1;
  m_history.resize(kHistorySize);
  m_receive_buffer.resize(kMaxDatagramSize);
  m_send_buffer.reserve(kMaxDatagramSize);
}

void Client::send_hello(double now) {
  write_hello(m_send_buffer, m_cookie);
  m_net.send(m_server, m_send_buffer.data(), m_send_buffer.size());
  m_last_hello = now;
}

void Client::poll(double now) {
  if (m_state != ClientState::kConnecting && m_state != ClientState::kConnected) return;
  if (!m_started) {
    m_started = true;
    m_last_heard = now;
    m_last_hello = now - m_config.hello_interval_seconds;  // send the first Hello right away
  }

  for (int i = 0; i < kMaxDatagramsPerPoll; ++i) {
    Address from;
    const std::size_t size = m_net.receive(from, m_receive_buffer.data(), m_receive_buffer.size());
    if (size == 0) break;
    if (from != m_server) continue;  // only the server may talk to us
    handle_datagram(m_receive_buffer.data(), size, now);
    if (m_state != ClientState::kConnecting && m_state != ClientState::kConnected) return;
  }
  // However many snapshots arrived, reconcile once, from the newest.
  if (m_snapshot_pending) reconcile(now);

  if (m_state == ClientState::kConnecting && now - m_last_hello >= m_config.hello_interval_seconds) {
    m_hello_pending = true;
    send_hello(now);
  }

  if (now - m_last_heard > m_config.timeout_seconds) {
    m_state = ClientState::kTimedOut;
    m_error = "no answer from the server at " + to_string(m_server);
  }
}

void Client::handle_datagram(const std::uint8_t* data, std::size_t size, double now) {
  ByteReader reader(data, size);
  MessageType type = MessageType::kHello;
  if (!read_header(reader, type)) return;

  switch (type) {
    case MessageType::kWelcome:
      handle_welcome(reader, now);
      break;
    case MessageType::kReject: {
      RejectReason reason = RejectReason::kServerFull;
      if (!read_reject(reader, reason) || m_state != ClientState::kConnecting) return;
      m_state = ClientState::kRejected;
      m_error = reject_reason_text(reason);
      break;
    }
    case MessageType::kSnapshotFragment:
      handle_fragment(reader, now);
      break;
    case MessageType::kChallenge: {
      // The server wants proof that we receive its packets: echo its cookie
      // at once (also while playing: asking again after a missed reload
      // needs a fresh cookie too). Only one answer per Hello we sent
      // ourselves, so faked Challenges cannot make us send Hellos on demand,
      // and a Challenge never counts as hearing from the server.
      std::uint64_t cookie = 0;
      if (!read_challenge(reader, cookie) || !m_hello_pending) return;
      if (m_state != ClientState::kConnecting && m_state != ClientState::kConnected) return;
      m_hello_pending = false;
      m_cookie = cookie;
      send_hello(now);
      break;
    }
    default:
      break;  // client-to-server messages are ignored
  }
}

void Client::handle_welcome(ByteReader& reader, double now) {
  WelcomeMessage welcome;
  if (!read_welcome(reader, welcome)) return;
  // The server's token for our address never changes: while playing, a
  // Welcome with another one is not from our server's session (faked).
  if (m_state == ClientState::kConnected && welcome.session != m_session) return;
  m_last_heard = now;
  if (m_state == ClientState::kConnecting) m_session = welcome.session;

  if (m_state == ClientState::kConnected) {
    // While playing, a Welcome is either a duplicate (ignore it) or the
    // server announcing a hot-reloaded scene (load it).
    if (welcome.scene_hash == m_scene_hash && welcome.scene_name == m_scene_name) return;
    if (!load_world(welcome)) return;
    return;
  }
  if (m_state != ClientState::kConnecting) return;
  if (!load_world(welcome)) return;
  m_state = ClientState::kConnected;
}

bool Client::load_world(const WelcomeMessage& welcome) {
  // The server names the scene; both sides load it from their own files and
  // the hash proves they are the same file.
  Scene scene;
  std::string error;
  if (!load_scene(m_config.scene_directory, welcome.scene_name, scene, error)) {
    m_state = ClientState::kFailed;
    m_error = "cannot load the server's scene: " + error;
    return false;
  }
  if (scene.content_hash != welcome.scene_hash) {
    m_state = ClientState::kFailed;
    m_error = "scene '" + welcome.scene_name + "' differs from the server's copy";
    return false;
  }

  m_player = welcome.player;
  m_scene_name = welcome.scene_name;
  m_scene_hash = welcome.scene_hash;
  m_authoritative = make_world(scene, welcome.rules);
  m_authoritative.tick = welcome.server_tick;
  m_current = m_authoritative;
  m_previous = m_authoritative;
  m_display = m_authoritative;
  // A copy gets only the capacity it needs right now; give the other worlds
  // the same headroom make_world reserved, so spawning during prediction and
  // copying for display do not allocate.
  for (World* world : {&m_current, &m_previous, &m_display}) {
    world->entities.reserve(m_authoritative.entities.capacity());
    world->free_slots.reserve(m_authoritative.free_slots.capacity());
  }
  // States of the old world are useless as baselines: start over from a full
  // snapshot. The server forgets its own on a reload, and our next Input
  // says "I hold nothing" (kFullSnapshot) in case it has not.
  for (KeptState& kept : m_kept) {
    kept.id = kFullSnapshot;
    reserve_state(kept.state, m_authoritative.entities.capacity());
  }
  m_acked_snapshot = kFullSnapshot;
  m_snapshot_pending = false;
  m_sample_count = 0;
  m_sample_next = 0;
  m_has_world = true;
  ++m_world_generation;
  return true;
}

void Client::handle_fragment(ByteReader& reader, double now) {
  FragmentHeader header;
  const std::uint8_t* payload = nullptr;
  std::size_t size = 0;
  if (!read_fragment(reader, header, payload, size)) return;
  m_last_heard = now;
  if (m_state != ClientState::kConnected) return;  // snapshots before Welcome are useless

  if (header.scene_hash != m_scene_hash) {
    // The server is running a different scene (a hot reload whose Welcome we
    // missed). Ask again: the server answers a known client's Hello with a
    // fresh Welcome.
    if (now - m_last_hello >= m_config.hello_interval_seconds) {
      m_hello_pending = true;
      send_hello(now);
    }
    return;
  }
  if (!m_assembler.add(header, payload, size)) return;

  // A complete snapshot: stage it. Reconciling waits until the end of poll().
  const SnapshotRead result = read_snapshot(m_assembler.data(), m_assembler.size());
  if (result == SnapshotRead::kNoBaseline) return;  // nothing touched: a staged snapshot stays
  if (result == SnapshotRead::kCorrupt) {
    // m_authoritative may be half-overwritten, so drop any staged one too
    // and wait for the next snapshot.
    m_snapshot_pending = false;
    return;
  }
  keep_snapshot(m_assembler.snapshot_id());
  m_snapshot_pending = true;
  m_snapshot_ack = m_assembler.ack_sequence();
  record_sample();
}

Client::SnapshotRead Client::read_snapshot(const std::uint8_t* data, std::size_t size) {
  ByteReader reader(data, size);
  const std::uint32_t baseline_id = reader.read_u32();
  if (!reader.ok()) return SnapshotRead::kNoBaseline;  // nothing read into the world yet
  if (baseline_id == kFullSnapshot) {
    if (!read_world_state(reader, m_authoritative) || reader.remaining() != 0) return SnapshotRead::kCorrupt;
    ++m_full_snapshots;
    return SnapshotRead::kOk;
  }
  const KeptState& kept = m_kept[baseline_id % kSnapshotWindow];
  if (kept.id != baseline_id) {
    // Not kept (any more, or never: the server may be answering an older
    // ack). Our ack keeps naming what we do have; once that is too old, the
    // server falls back to a full snapshot.
    ++m_missing_baselines;
    return SnapshotRead::kNoBaseline;
  }
  if (!read_world_delta(reader, kept.state, m_authoritative) || reader.remaining() != 0) {
    return SnapshotRead::kCorrupt;
  }
  ++m_delta_snapshots;
  return SnapshotRead::kOk;
}

void Client::keep_snapshot(std::uint32_t snapshot_id) {
  KeptState& kept = m_kept[snapshot_id % kSnapshotWindow];
  kept.id = snapshot_id;
  copy_state(m_authoritative, kept.state);
  m_acked_snapshot = snapshot_id;
}

void Client::record_sample() {
  const World& world = m_authoritative;
  if (m_sample_count > 0) {
    const SnapshotSample& newest = m_samples[(m_sample_next + kSampleCount - 1) % kSampleCount];
    if (world.tick <= newest.tick) return;  // never out of order
  }
  SnapshotSample& sample = m_samples[m_sample_next];
  sample.tick = world.tick;
  for (std::size_t p = 0; p < sample.players.size(); ++p) {
    const EntityId id = world.player_entities[p];
    const Entity* entity = find_entity(world, id);
    sample.players[p].id = entity != nullptr ? id : EntityId();
    sample.players[p].position = entity != nullptr ? entity->position : Vec3();
  }
  m_sample_next = (m_sample_next + 1) % kSampleCount;
  if (m_sample_count < kSampleCount) ++m_sample_count;

  // Keep the interpolation clock a fixed delay behind the newest snapshot.
  const double target = static_cast<double>(world.tick) - static_cast<double>(m_config.interpolation_delay_ticks);
  if (m_sample_count == 1 || std::fabs(m_interpolation_tick - target) > kInterpolationSnapTicks) {
    m_interpolation_tick = target;
  } else {
    m_interpolation_tick += (target - m_interpolation_tick) * 0.1;  // drift gently, no visible jumps
  }
}

bool Client::sample_position(EntityId id, double tick, Vec3& out) const {
  // Walk the samples oldest to newest; find the two around `tick`.
  bool have_before = false;
  Vec3 before;
  double before_tick = 0.0;
  for (std::size_t n = 0; n < m_sample_count; ++n) {
    const SnapshotSample& sample = m_samples[(m_sample_next + kSampleCount - m_sample_count + n) % kSampleCount];
    const PlayerSample* found = nullptr;
    for (const PlayerSample& player : sample.players) {
      if (player.id.is_valid() && player.id == id) {
        found = &player;
        break;
      }
    }
    if (found == nullptr) continue;
    const double sample_tick = static_cast<double>(sample.tick);
    if (sample_tick <= tick) {
      have_before = true;
      before = found->position;
      before_tick = sample_tick;
      continue;
    }
    if (!have_before) {
      out = found->position;  // render time is before everything we have
      return true;
    }
    const float t = static_cast<float>((tick - before_tick) / (sample_tick - before_tick));
    out = lerp(before, found->position, t);
    return true;
  }
  if (!have_before) return false;
  out = before;  // render time is past the newest snapshot: hold, do not guess
  return true;
}

const World& Client::display_world(float alpha) {
  m_display = m_current;  // capacity is reserved: no allocation
  const double render_tick = m_interpolation_tick + static_cast<double>(alpha);
  for (std::size_t i = 0; i < m_display.entities.size(); ++i) {
    Entity& entity = m_display.entities[i];
    if (!entity.alive) continue;

    if (entity.player != kNoPlayer && entity.player != m_player && m_sample_count > 0) {
      Vec3 position;
      if (sample_position(entity_id_at(m_current, static_cast<std::uint32_t>(i)), render_tick, position)) {
        entity.position = position;
        continue;
      }
    }
    if (i < m_previous.entities.size()) {
      const Entity& before = m_previous.entities[i];
      if (before.alive && before.generation == entity.generation) {
        entity.position = lerp(before.position, entity.position, alpha);
      }
    }
  }
  return m_display;
}

void Client::reconcile(double now) {
  m_snapshot_pending = false;
  const std::uint32_t ack = m_snapshot_ack;
  const std::uint32_t newest = m_next_sequence - 1;
  // An ack can never be ahead of what we sent; ignore one that claims to be.
  if (sequence_newer(ack, m_last_acked) && !sequence_newer(ack, newest)) {
    const HistoryEntry& acked = m_history[ack % kHistorySize];
    if (acked.frame.sequence == ack) {
      const double sample = now - acked.sent_at;
      m_round_trip = m_round_trip <= 0.0 ? sample : m_round_trip * 0.9 + sample * 0.1;
    }
    m_last_acked = ack;
  }

  // Reconcile: start from the server's truth and replay what it has not seen.
  // m_previous is left alone, so rendering blends from the old prediction to
  // the corrected one instead of snapping.
  m_current = m_authoritative;
  std::uint32_t first = m_last_acked + 1;
  if (newest - m_last_acked > kHistorySize) first = newest - static_cast<std::uint32_t>(kHistorySize) + 1;
  for (std::uint32_t sequence = first; sequence != m_next_sequence; ++sequence) {
    const HistoryEntry& entry = m_history[sequence % kHistorySize];
    if (entry.frame.sequence != sequence) continue;  // older than the history window
    m_simulate(m_current, entry.frame.commands, m_current.tick + 1, m_config.jobs);
  }
}

void Client::tick(const CommandList& commands, double now) {
  if (m_state != ClientState::kConnected) return;

  const std::uint32_t sequence = m_next_sequence++;
  HistoryEntry& entry = m_history[sequence % kHistorySize];
  entry.frame.sequence = sequence;
  entry.frame.commands.clear();
  entry.sent_at = now;
  for (const Command& command : commands) {
    if (entry.frame.commands.size() >= kMaxCommandsPerInputFrame) break;
    if (command.type == CommandType::kNone || is_server_only(command.type)) continue;
    Command own = command;
    own.player = m_player;
    entry.frame.commands.push(own);
  }

  send_inputs();

  m_previous = m_current;
  m_simulate(m_current, entry.frame.commands, m_current.tick + 1, m_config.jobs);
  m_interpolation_tick += 1.0;  // the past we draw other players in moves at game speed
}

void Client::send_inputs() {
  // Send the newest frames the server has not acknowledged, oldest first.
  const InputFrame* frames[kMaxInputFramesPerPacket] = {};
  const std::uint32_t newest = m_next_sequence - 1;
  std::uint32_t unacked = newest - m_last_acked;
  if (unacked == 0) return;
  if (unacked > kMaxInputFramesPerPacket) unacked = static_cast<std::uint32_t>(kMaxInputFramesPerPacket);

  std::size_t count = 0;
  for (std::uint32_t sequence = newest - unacked + 1;; ++sequence) {
    const HistoryEntry& entry = m_history[sequence % kHistorySize];
    if (entry.frame.sequence == sequence) frames[count++] = &entry.frame;
    if (sequence == newest) break;
  }
  if (count == 0) return;
  write_input(m_send_buffer, m_session, m_acked_snapshot, frames, count);
  m_net.send(m_server, m_send_buffer.data(), m_send_buffer.size());
}

void Client::disconnect() {
  if (m_state == ClientState::kConnected || m_state == ClientState::kConnecting) {
    write_bye(m_send_buffer, m_session);
    m_net.send(m_server, m_send_buffer.data(), m_send_buffer.size());
  }
  m_state = ClientState::kDisconnected;
}

}  // namespace scraps
