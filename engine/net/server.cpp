#include "net/server.h"

#include <utility>

namespace scraps {
namespace {

constexpr int kMaxDatagramsPerPoll = 4096;  // bounds the work one poll() can do under a flood

}  // namespace

Server::Server(Net& net, World world, SimulateFn simulate, std::string scene_name, std::uint32_t scene_hash,
               ServerConfig config)
    : m_net(net),
      m_world(std::move(world)),
      m_simulate(simulate != nullptr ? simulate : &scraps::simulate),
      m_scene_name(std::move(scene_name)),
      m_scene_hash(scene_hash),
      m_config(config),
      m_hello_limiter(config.hello_replies_per_address),
      m_hello_total(config.hello_replies_total) {
  if (m_config.max_players < 1) m_config.max_players = 1;
  if (m_config.max_players > kMaxPlayers) m_config.max_players = kMaxPlayers;
  if (m_config.snapshot_interval_ticks < 1) m_config.snapshot_interval_ticks = 1;
  m_receive_buffer.resize(kMaxDatagramSize);
  m_send_buffer.reserve(kMaxDatagramSize);
  forget_snapshots();
}

void Server::forget_snapshots() {
  // Called at start and on hot reload: states of another world are useless
  // as baselines, so every client gets a full snapshot next.
  for (SentState& sent : m_sent) {
    sent.id = kFullSnapshot;
    reserve_state(sent.state, m_world.entities.capacity());
  }
  for (ClientSlot& client : m_clients) client.acked_snapshot = kFullSnapshot;
}

const WorldState* Server::baseline(std::uint32_t snapshot_id) const {
  if (snapshot_id == kFullSnapshot) return nullptr;
  const SentState& sent = m_sent[snapshot_id % kSnapshotWindow];
  return sent.id == snapshot_id ? &sent.state : nullptr;
}

std::size_t Server::last_snapshot_size(PlayerId player) const {
  if (player >= kMaxPlayers) return 0;
  return m_clients[player].last_snapshot_size;
}

int Server::client_count() const {
  int count = 0;
  for (const ClientSlot& client : m_clients) {
    if (client.connected && client.confirmed) ++count;
  }
  return count;
}

int Server::reserved_slots() const {
  int count = 0;
  for (const ClientSlot& client : m_clients) {
    if (client.connected) ++count;
  }
  return count;
}

int Server::find_client(const Address& address) const {
  for (int i = 0; i < kMaxPlayers; ++i) {
    const ClientSlot& client = m_clients[static_cast<std::size_t>(i)];
    if (client.connected && client.address == address) return i;
  }
  return -1;
}

void Server::push_command(const Command& command) {
  if (!m_tick_commands.push(command)) ++m_dropped_commands;
}

void Server::poll(double now) {
  for (int i = 0; i < kMaxDatagramsPerPoll; ++i) {
    Address from;
    const std::size_t size = m_net.receive(from, m_receive_buffer.data(), m_receive_buffer.size());
    if (size == 0) break;
    handle_datagram(from, m_receive_buffer.data(), size, now);
  }

  if (m_paused || m_restart_timeouts) {
    // Timeouts are frozen while paused: a paused client sends nothing, and
    // must not be dropped as gone.
    for (ClientSlot& client : m_clients) {
      if (client.connected) client.last_heard = now;
    }
    m_restart_timeouts = false;
  }

  for (int i = 0; i < kMaxPlayers; ++i) {
    const ClientSlot& client = m_clients[static_cast<std::size_t>(i)];
    if (!client.connected) continue;
    const double timeout = client.confirmed ? m_config.client_timeout_seconds : kUnconfirmedTimeoutSeconds;
    if (now - client.last_heard > timeout) disconnect(i);
  }
}

void Server::handle_datagram(const Address& from, const std::uint8_t* data, std::size_t size, double now) {
  ByteReader reader(data, size);
  MessageType type = MessageType::kHello;
  if (!read_header(reader, type)) return;  // not ours, or corrupt

  if (type == MessageType::kHello) {
    handle_hello(from, reader, now);
    return;
  }

  const int slot = find_client(from);
  if (slot < 0) return;  // unknown sender
  ClientSlot& client = m_clients[static_cast<std::size_t>(slot)];
  // Everything a player sends starts with its session token, which only the
  // address that received the Welcome knows. Without it the datagram is
  // someone faking that address: it changes nothing and keeps nothing alive.
  ByteReader token = reader;
  if (token.read_u64() != client.session || !token.ok()) {
    ++m_forged_datagrams;
    return;
  }
  if (!client.datagrams.take(now)) {
    ++m_client_datagrams_dropped;  // a flood: the work any one client can cause is bounded
    return;
  }
  client.last_heard = now;

  switch (type) {
    case MessageType::kInput: handle_input(slot, reader); break;
    case MessageType::kBye: disconnect(slot); break;
    default: break;  // server-to-client messages are ignored
  }
}

bool Server::may_answer_stranger(const Address& to, double now) {
  // Per address first, so one noisy address cannot use up the shared budget.
  if (m_hello_limiter.allow(to.ip, now) && m_hello_total.take(now)) return true;
  ++m_hellos_rate_limited;
  return false;
}

bool Server::cookie_valid(const Address& from, std::uint64_t cookie, double now) const {
  const std::uint32_t window = cookie_window(now);
  return cookie != 0 && (cookie == make_cookie(m_config.cookie_secret, from, window) ||
                         (window > 0 && cookie == make_cookie(m_config.cookie_secret, from, window - 1)));
}

void Server::send_challenge(const Address& to, double now) {
  write_challenge(m_send_buffer, make_cookie(m_config.cookie_secret, to, cookie_window(now)));
  m_net.send(to, m_send_buffer.data(), m_send_buffer.size());
  ++m_challenges_sent;
}

void Server::handle_hello(const Address& from, ByteReader& reader, double now) {
  if (!accepts_players()) return;  // no secret: cookies could be forged, so nobody gets in

  // Another protocol version gets a 6-byte Reject, whatever the size of its
  // Hello (older ones were shorter): still never more than it sent.
  ByteReader version_reader = reader;
  const std::uint16_t claimed_version = version_reader.read_u16();
  if (!version_reader.ok()) return;
  if (claimed_version != kProtocolVersion) {
    const bool known = find_client(from) >= 0;
    if (known && !m_hello_limiter.allow(from.ip, now)) {
      ++m_hellos_rate_limited;
      return;
    }
    if (!known && !may_answer_stranger(from, now)) return;
    write_reject(m_send_buffer, RejectReason::kVersionMismatch);
    m_net.send(from, m_send_buffer.data(), m_send_buffer.size());
    return;
  }

  std::uint16_t version = 0;
  std::uint64_t cookie = 0;
  if (!read_hello(reader, version, cookie)) return;  // short Hellos are never answered

  const int existing = find_client(from);
  if (existing >= 0) {
    // A player asking again (a lost Welcome, or a reload it missed). It
    // proves its address like anyone else, but draws only on its own
    // address's budget, so a flood of strangers cannot starve it. A Hello
    // never keeps a slot alive: only datagrams with the session token do.
    if (!m_hello_limiter.allow(from.ip, now)) {
      ++m_hellos_rate_limited;
      return;
    }
    if (cookie_valid(from, cookie, now)) {
      send_welcome(existing);
    } else {
      send_challenge(from, now);
    }
    return;
  }

  if (!may_answer_stranger(from, now)) return;

  // Only an address that receives our packets can echo its cookie, so a
  // faked address stops here, with one reply smaller than its Hello.
  if (!cookie_valid(from, cookie, now)) {
    send_challenge(from, now);
    return;
  }

  int slot = -1;
  for (int i = 0; i < m_config.max_players; ++i) {
    if (!m_clients[static_cast<std::size_t>(i)].connected) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    write_reject(m_send_buffer, RejectReason::kServerFull);
    m_net.send(from, m_send_buffer.data(), m_send_buffer.size());
    return;
  }

  ClientSlot& client = m_clients[static_cast<std::size_t>(slot)];
  client = ClientSlot();
  client.connected = true;
  client.address = from;
  client.session = make_session_token(m_config.cookie_secret, from);
  client.last_heard = now;
  client.datagrams = TokenBucket(m_config.client_datagrams);
  client.datagrams.reset(now);
  send_welcome(slot);
}

void Server::send_welcome(int slot) {
  WelcomeMessage welcome;
  welcome.player = static_cast<PlayerId>(slot);
  welcome.session = m_clients[static_cast<std::size_t>(slot)].session;
  welcome.server_tick = m_world.tick;
  welcome.scene_hash = m_scene_hash;
  welcome.scene_name = m_scene_name;
  welcome.rules = m_world.rules;
  write_welcome(m_send_buffer, welcome);
  m_net.send(m_clients[static_cast<std::size_t>(slot)].address, m_send_buffer.data(), m_send_buffer.size());
}

void Server::handle_input(int slot, ByteReader& reader) {
  if (!read_input(reader, m_input_message)) return;
  ClientSlot& client = m_clients[static_cast<std::size_t>(slot)];
  // A made-up id only hurts the client that sends it: it is not among the
  // kept states (a full snapshot follows), or the client cannot read the delta.
  client.acked_snapshot = updated_snapshot_ack(client.acked_snapshot, m_input_message.acked_snapshot);

  if (!client.confirmed) {
    // The first Input proves the address is a real client (it received our
    // Welcome). Only now does the player's entity get created, by simulate,
    // like every other change (Rule 1).
    client.confirmed = true;
    m_pending_join[static_cast<std::size_t>(slot)] = true;
  }

  for (std::size_t i = 0; i < m_input_message.count; ++i) {
    const InputFrame& frame = m_input_message.frames[i];
    // Each packet repeats recent frames; only frames we have not seen are queued.
    if (!sequence_newer(frame.sequence, client.last_received)) continue;
    if (!sequence_newer(frame.sequence, client.last_applied)) continue;

    if (client.queue_count == kInputQueueCapacity) {
      // Full: the oldest frame is skipped, as if it had been lost. The client
      // predicted it, so it will see a small correction.
      client.last_applied = client.queue[client.queue_head].sequence;
      client.queue_head = (client.queue_head + 1) % kInputQueueCapacity;
      --client.queue_count;
    }

    InputFrame& queued = client.queue[(client.queue_head + client.queue_count) % kInputQueueCapacity];
    queued.sequence = frame.sequence;
    queued.commands.clear();
    for (const Command& command : frame.commands) {
      // Server authority: a client can only speak for itself, and cannot use
      // server-only commands.
      if (command.type == CommandType::kNone || is_server_only(command.type)) continue;
      Command checked = command;
      checked.player = static_cast<PlayerId>(slot);
      queued.commands.push(checked);
    }
    ++client.queue_count;
    client.last_received = frame.sequence;
  }
}

void Server::disconnect(int slot) {
  ClientSlot& client = m_clients[static_cast<std::size_t>(slot)];
  if (!client.connected) return;
  const bool had_entity = client.confirmed;
  client.connected = false;
  client.confirmed = false;
  client.queue_count = 0;
  const std::size_t index = static_cast<std::size_t>(slot);
  m_pending_join[index] = false;
  if (had_entity) m_pending_leave[index] = true;
}

void Server::set_paused(bool paused) {
  if (m_paused && !paused) m_restart_timeouts = true;
  m_paused = paused;
}

void Server::tick() {
  if (m_paused) return;  // no tick, no snapshot (Rule 2: a pause stops ticks)
  m_tick_commands.clear();

  // Joins and leaves first; at most two per slot, so they always fit.
  for (std::size_t i = 0; i < m_clients.size(); ++i) {
    const PlayerId player = static_cast<PlayerId>(i);
    if (m_pending_leave[i]) push_command(make_command(CommandType::kPlayerLeave, player));
    if (m_pending_join[i]) push_command(make_command(CommandType::kPlayerJoin, player));
    m_pending_leave[i] = false;
    m_pending_join[i] = false;
  }

  for (ClientSlot& client : m_clients) {
    if (!client.connected || client.queue_count == 0) continue;
    // Normally one frame per tick. A client whose queue has grown (its clock
    // runs a little fast, or packets arrived in a burst) gets two, to catch
    // up. The client predicted those as two ticks, so it sees one tick of
    // correction; that is the price of keeping latency from growing.
    const std::size_t frames = client.queue_count > kInputQueueTarget ? 2 : 1;
    // No client may use more than its share of the tick's command list.
    std::size_t budget = kMaxCommandsPerInputFrame;
    for (std::size_t f = 0; f < frames && client.queue_count > 0; ++f) {
      const InputFrame& frame = client.queue[client.queue_head];
      for (const Command& command : frame.commands) {
        if (budget == 0) {
          ++m_dropped_commands;
          continue;
        }
        push_command(command);
        --budget;
      }
      client.last_applied = frame.sequence;
      client.queue_head = (client.queue_head + 1) % kInputQueueCapacity;
      --client.queue_count;
    }
  }

  m_simulate(m_world, m_tick_commands, m_world.tick + 1, m_config.jobs);

  if (++m_ticks_since_snapshot >= m_config.snapshot_interval_ticks) {
    m_ticks_since_snapshot = 0;
    send_snapshots();
  }
}

void Server::reload(World world, std::string scene_name, std::uint32_t scene_hash) {
  world.tick = m_world.tick;  // ticks never go backwards on a hot reload
  replace_world(std::move(world), std::move(scene_name), scene_hash);
}

void Server::load_state(World world, std::string scene_name, std::uint32_t scene_hash) {
  // The saved tick stays. A client notices the tick going back in the next
  // full snapshot and restarts its prediction and interpolation from it.
  replace_world(std::move(world), std::move(scene_name), scene_hash);
}

void Server::replace_world(World world, std::string scene_name, std::uint32_t scene_hash) {
  m_world = std::move(world);
  m_scene_name = std::move(scene_name);
  m_scene_hash = scene_hash;
  for (std::size_t i = 0; i < m_clients.size(); ++i) {
    const ClientSlot& client = m_clients[i];
    const bool playing = client.connected && client.confirmed;
    // The old world, with every entity in it, is gone. The new one may hold
    // players (a loaded save): those who are playing now keep theirs (a join
    // of a player who has an entity changes nothing), the others' are removed.
    m_pending_leave[i] = !playing && find_entity(m_world, player_entity(m_world, static_cast<PlayerId>(i))) != nullptr;
    if (playing) m_pending_join[i] = true;
    if (client.connected) send_welcome(static_cast<int>(i));
  }
  forget_snapshots();
}

void Server::send_snapshots() {
  if (client_count() == 0) return;

  // Snapshot ids start at 1 and skip 0 when they wrap: 0 means "full".
  ++m_snapshot_id;
  if (m_snapshot_id == kFullSnapshot) ++m_snapshot_id;
  m_full_bytes.clear();

  for (ClientSlot& client : m_clients) {
    if (!client.connected || !client.confirmed) continue;
    // Delta against the newest snapshot this client holds, if it is still
    // kept; a full state otherwise (just joined, reloaded, or long silent).
    const WorldState* base = baseline(client.acked_snapshot);
    if (base != nullptr) {
      m_delta_bytes.clear();
      ByteWriter writer(m_delta_bytes);
      writer.write_u32(client.acked_snapshot);
      write_world_delta(writer, *base, m_world);
      send_snapshot(client, m_delta_bytes);
      ++m_delta_snapshots;
    } else {
      if (m_full_bytes.empty()) {  // once per snapshot, shared by every client that needs it
        ByteWriter writer(m_full_bytes);
        writer.write_u32(kFullSnapshot);
        write_world_state(writer, m_world);
      }
      send_snapshot(client, m_full_bytes);
      ++m_full_snapshots;
    }
  }

  // Keep this state as a baseline for later deltas. Stored after encoding so
  // it never replaces the baseline (32 snapshots old) a client is using now.
  SentState& sent = m_sent[m_snapshot_id % kSnapshotWindow];
  sent.id = m_snapshot_id;
  copy_state(m_world, sent.state);
}

void Server::send_snapshot(ClientSlot& client, const std::vector<std::uint8_t>& payload) {
  const std::size_t total = payload.size();
  if (total == 0 || total > kMaxSnapshotSize) {
    ++m_unsent_snapshots;  // too big to send; scenes are capped so this should not happen
    return;
  }
  client.last_snapshot_size = total;
  const std::size_t count = (total + kFragmentPayloadSize - 1) / kFragmentPayloadSize;
  for (std::size_t i = 0; i < count; ++i) {
    FragmentHeader header;
    header.snapshot_id = m_snapshot_id;
    header.index = static_cast<std::uint16_t>(i);
    header.count = static_cast<std::uint16_t>(count);
    header.ack_sequence = client.last_applied;
    header.scene_hash = m_scene_hash;
    const std::size_t offset = i * kFragmentPayloadSize;
    const std::size_t size = total - offset < kFragmentPayloadSize ? total - offset : kFragmentPayloadSize;
    write_fragment(m_send_buffer, header, payload.data() + offset, size);
    m_net.send(client.address, m_send_buffer.data(), m_send_buffer.size());
  }
}

}  // namespace scraps
