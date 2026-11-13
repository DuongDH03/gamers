#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/scene.h"
#include "harness.h"
#include "net/client.h"
#include "net/cookie.h"
#include "net/loopback.h"
#include "net/protocol.h"
#include "net/server.h"
#include "sandbox/sandbox.h"
#include "views/view_factory.h"

using namespace scraps;

namespace {

constexpr double kFrame = 1.0 / 60.0;

// A server and any number of clients on one LoopbackHub, stepped in lockstep.
class Rig {
 public:
  // A fixed cookie secret unless the test sets its own (or, with
  // `keep_zero_secret`, wants to see what a server without one does).
  explicit Rig(ServerConfig config = ServerConfig(), const char* scene_name = "top-down",
               const std::string& scene_dir = SCRAPS_SANDBOX_SCENE_DIR, bool keep_zero_secret = false)
      : server_net(hub, 1) {
    if (!keep_zero_secret && !is_set(config.cookie_secret)) config.cookie_secret = CookieSecret{0x0123456789ABCDEFull, 0x1122334455667788ull};
    secret = config.cookie_secret;
    Scene scene;
    std::string error;
    if (!load_scene(scene_dir, scene_name, scene, error)) std::printf("rig: %s\n", error.c_str());
    World world = make_world(scene, movement_rules_for(scene.level.perspective));
    server = std::make_unique<Server>(server_net, std::move(world), &sandbox::simulate, scene.name,
                                      scene.content_hash, config);
  }

  // Each client plays its own machine (its own IP), unless `ip` says otherwise:
  // rate limits are per IP address.
  Client& add_client(const std::string& scene_dir = SCRAPS_SANDBOX_SCENE_DIR, std::uint32_t ip = 0) {
    const std::uint32_t own_ip = ip != 0 ? ip : 0x0A000001u + static_cast<std::uint32_t>(nets.size());
    nets.push_back(std::make_unique<LoopbackNet>(hub, Address(own_ip, static_cast<std::uint16_t>(100 + nets.size()))));
    ClientConfig config;
    config.scene_directory = scene_dir;
    clients.push_back(
        std::make_unique<Client>(*nets.back(), server_net.local_address(), &sandbox::simulate, config));
    commands.emplace_back();
    return *clients.back();
  }

  // One 60 Hz frame: clients poll and tick, the server polls and ticks.
  void frame(bool run_server = true) {
    now += kFrame;
    hub.set_now(now);
    for (std::size_t i = 0; i < clients.size(); ++i) {
      clients[i]->poll(now);
      clients[i]->tick(commands[i], now);
    }
    if (run_server) {
      server->poll(now);
      server->tick();
    }
  }
  void frames(int count, bool run_server = true) {
    for (int i = 0; i < count; ++i) frame(run_server);
  }

  const Entity* server_player(PlayerId player) const {
    return find_entity(server->world(), player_entity(server->world(), player));
  }
  static const Entity* client_player(const Client& client) {
    return find_entity(client.current_world(), client.local_entity());
  }

  // Declaration order matters: the hub outlives the endpoints, and the
  // endpoints outlive the server and clients that use them.
  LoopbackHub hub;
  LoopbackNet server_net;
  CookieSecret secret;
  std::vector<std::unique_ptr<LoopbackNet>> nets;
  std::unique_ptr<Server> server;
  std::vector<std::unique_ptr<Client>> clients;
  std::vector<CommandList> commands;  // what each client sends every tick
  double now = 0.0;
};

const Address kServerAddress(kLocalhostIp, 1);

// Reads every datagram waiting at `net`; returns the last Challenge's cookie
// (0 if none), counts what arrived, and notes a Welcome's session token.
std::uint64_t drain(LoopbackNet& net, std::size_t* count = nullptr, std::size_t* largest = nullptr,
                    std::uint64_t* session = nullptr) {
  std::uint64_t cookie = 0;
  std::uint8_t buffer[kMaxDatagramSize];
  Address from;
  for (std::size_t size = net.receive(from, buffer, sizeof(buffer)); size > 0;
       size = net.receive(from, buffer, sizeof(buffer))) {
    if (count != nullptr) ++*count;
    if (largest != nullptr && size > *largest) *largest = size;
    ByteReader reader(buffer, size);
    MessageType type = MessageType::kHello;
    if (!read_header(reader, type)) continue;
    if (type == MessageType::kChallenge) read_challenge(reader, cookie);
    WelcomeMessage welcome;
    if (type == MessageType::kWelcome && session != nullptr && read_welcome(reader, welcome)) *session = welcome.session;
  }
  return cookie;
}

// The first half of a hand-made handshake: Hello without a cookie, and the
// server's Challenge back. Returns the cookie (0 if no Challenge came).
std::uint64_t ask_for_cookie(class Rig& rig, LoopbackNet& raw);

CommandList move_command(Vec2 direction) {
  CommandList list;
  list.push(make_command(CommandType::kMove, 0, Vec3(direction.x, direction.y, 0.0f)));
  return list;
}

std::uint64_t ask_for_cookie(Rig& rig, LoopbackNet& raw) {
  std::vector<std::uint8_t> bytes;
  write_hello(bytes, 0);
  raw.send(kServerAddress, bytes.data(), bytes.size());
  rig.frame();
  return drain(raw);
}

// The whole hand-made handshake: afterwards `raw` holds a reserved slot and
// its first Input will make it a player. Returns the session token from the
// Welcome (0 if it failed).
std::uint64_t raw_join(Rig& rig, LoopbackNet& raw) {
  const std::uint64_t cookie = ask_for_cookie(rig, raw);
  if (cookie == 0) return 0;
  std::vector<std::uint8_t> bytes;
  write_hello(bytes, cookie);
  raw.send(kServerAddress, bytes.data(), bytes.size());
  rig.frame();
  std::uint64_t session = 0;
  drain(raw, nullptr, nullptr, &session);
  return session;
}

void two_clients_share_one_world(test::Context& ctx) {
  Rig rig;
  Client& a = rig.add_client();
  Client& b = rig.add_client();
  rig.frames(10);
  CHECK(a.state() == ClientState::kConnected);
  CHECK(b.state() == ClientState::kConnected);
  CHECK(a.player_id() != b.player_id());
  CHECK(rig.server->client_count() == 2);
  CHECK(Rig::client_player(a) != nullptr);
  CHECK(Rig::client_player(b) != nullptr);
  // Each client sees both players.
  CHECK(find_entity(a.current_world(), player_entity(a.current_world(), b.player_id())) != nullptr);
  CHECK(find_entity(b.current_world(), player_entity(b.current_world(), a.player_id())) != nullptr);
}

void inputs_move_the_authoritative_player(test::Context& ctx) {
  Rig rig;
  Client& client = rig.add_client();
  rig.frames(5);
  const Entity* start = rig.server_player(client.player_id());
  CHECK(start != nullptr);
  if (start == nullptr) return;
  const float start_x = start->position.x;
  rig.commands[0] = move_command(Vec2(1.0f, 0.0f));
  rig.frames(30);
  rig.commands[0] = move_command(Vec2(0.0f, 0.0f));
  rig.frames(5);
  const Entity* server_side = rig.server_player(client.player_id());
  const Entity* client_side = Rig::client_player(client);
  CHECK(server_side != nullptr && client_side != nullptr);
  if (server_side == nullptr || client_side == nullptr) return;
  CHECK(server_side->position.x > start_x + 2.0f);
  CHECK_NEAR(client_side->position.x, server_side->position.x, 1e-4);
}

void prediction_hides_latency(test::Context& ctx) {
  Rig rig;
  rig.hub.set_latency(0.1);  // 100 ms each way
  Client& client = rig.add_client();
  rig.frames(60);
  CHECK(client.state() == ClientState::kConnected);
  const Entity* before = Rig::client_player(client);
  CHECK(before != nullptr);
  if (before == nullptr) return;
  const float client_x = before->position.x;
  const float server_x = rig.server_player(client.player_id())->position.x;

  rig.commands[0] = move_command(Vec2(1.0f, 0.0f));
  rig.frames(3);
  // The client moved at once; the server has not even received the input.
  CHECK(Rig::client_player(client)->position.x > client_x + 0.2f);
  CHECK_NEAR(rig.server_player(client.player_id())->position.x, server_x, 1e-6);
  CHECK(client.unacknowledged_inputs() > 0);

  rig.commands[0] = move_command(Vec2(0.0f, 0.0f));
  rig.frames(60);  // let everything arrive
  CHECK_NEAR(Rig::client_player(client)->position.x, rig.server_player(client.player_id())->position.x, 1e-4);
  CHECK(client.round_trip_seconds() > 0.15 && client.round_trip_seconds() < 0.3);
}

void lossy_network_still_converges(test::Context& ctx) {
  Rig rig;
  rig.hub.set_drop_rate(0.2);
  rig.hub.set_latency(0.05);
  Client& client = rig.add_client();
  rig.frames(60);
  CHECK(client.state() == ClientState::kConnected);
  rig.commands[0] = move_command(Vec2(0.0f, 1.0f));
  rig.frames(60);
  rig.commands[0] = move_command(Vec2(0.0f, 0.0f));
  rig.frames(120);
  CHECK(rig.hub.dropped() > 0);
  const Entity* client_side = Rig::client_player(client);
  const Entity* server_side = rig.server_player(client.player_id());
  CHECK(client_side != nullptr && server_side != nullptr);
  if (client_side == nullptr || server_side == nullptr) return;
  CHECK(server_side->position.y > 3.5f);  // redundant inputs got through
  CHECK_NEAR(client_side->position.y, server_side->position.y, 1e-3);
}

void a_full_server_rejects(test::Context& ctx) {
  ServerConfig config;
  config.max_players = 1;
  Rig rig(config);
  Client& first = rig.add_client();
  Client& second = rig.add_client();
  rig.frames(10);
  CHECK(first.state() == ClientState::kConnected);
  CHECK(second.state() == ClientState::kRejected);
  CHECK(second.error().find("full") != std::string::npos);
}

void wrong_protocol_version_is_rejected(test::Context& ctx) {
  Rig rig;
  LoopbackNet raw(rig.hub, 50);
  std::vector<std::uint8_t> bytes;
  write_hello(bytes, 0);
  bytes[5] = static_cast<std::uint8_t>((kProtocolVersion + 1) & 0xFF);  // the version, little-endian
  bytes[6] = static_cast<std::uint8_t>((kProtocolVersion + 1) >> 8);
  raw.send(Address(kLocalhostIp, 1), bytes.data(), bytes.size());
  rig.frame();

  std::uint8_t buffer[kMaxDatagramSize];
  Address from;
  const std::size_t size = raw.receive(from, buffer, sizeof(buffer));
  ByteReader reader(buffer, size);
  MessageType type = MessageType::kHello;
  CHECK(read_header(reader, type));
  CHECK(type == MessageType::kReject);
  RejectReason reason = RejectReason::kServerFull;
  CHECK(read_reject(reader, reason) && reason == RejectReason::kVersionMismatch);
  CHECK(rig.server->client_count() == 0);

  // A version-3 client's Hello was only 7 bytes: it still gets a (6-byte)
  // Reject, so the player reads "versions differ" instead of silence.
  std::vector<std::uint8_t> old_hello;
  ByteWriter writer(old_hello);
  writer.write_u32(kProtocolMagic);
  writer.write_u8(static_cast<std::uint8_t>(MessageType::kHello));
  writer.write_u16(3);
  raw.send(Address(kLocalhostIp, 1), old_hello.data(), old_hello.size());
  rig.frame();
  const std::size_t reply = raw.receive(from, buffer, sizeof(buffer));
  CHECK(reply > 0 && reply <= old_hello.size());
  ByteReader old_reader(buffer, reply);
  CHECK(read_header(old_reader, type) && type == MessageType::kReject);
}

void a_different_scene_file_is_refused(test::Context& ctx) {
  // A copy of the server's scene with one extra comment line: same name,
  // different contents.
  namespace fs = std::filesystem;
  const fs::path dir = fs::u8path(SCRAPS_TEST_WORK_DIR) / "scraps_test_scenes";
  std::error_code ignored;
  fs::create_directories(dir, ignored);
  {
    std::ifstream original(std::string(SCRAPS_SANDBOX_SCENE_DIR) + "/top-down.scene", std::ios::binary);
    std::ofstream copy(dir / "top-down.scene", std::ios::binary);
    copy << original.rdbuf() << "# edited\n";
  }
  Rig rig;
  Client& client = rig.add_client(dir.u8string());
  rig.frames(10);
  CHECK(client.state() == ClientState::kFailed);
  CHECK(client.error().find("differs") != std::string::npos);
  fs::remove_all(dir, ignored);
}

void silence_times_out_both_ways(test::Context& ctx) {
  Rig rig;
  Client& client = rig.add_client();
  rig.frames(10);
  CHECK(client.state() == ClientState::kConnected);

  rig.frames(6 * 60, false);  // the server stops answering for 6 s
  CHECK(client.state() == ClientState::kTimedOut);

  // The server, now running again, hears nothing from that client and drops it.
  for (int i = 0; i < 6 * 60; ++i) {
    rig.now += kFrame;
    rig.hub.set_now(rig.now);
    rig.server->poll(rig.now);
    rig.server->tick();
  }
  CHECK(rig.server->client_count() == 0);
  CHECK(count_alive(rig.server->world()) > 0);
  CHECK(!player_entity(rig.server->world(), client.player_id()).is_valid());
}

void bye_removes_the_player(test::Context& ctx) {
  Rig rig;
  Client& client = rig.add_client();
  rig.frames(10);
  const PlayerId id = client.player_id();
  CHECK(rig.server_player(id) != nullptr);
  client.disconnect();
  CHECK(client.state() == ClientState::kDisconnected);
  rig.frames(2);
  CHECK(rig.server->client_count() == 0);
  CHECK(rig.server_player(id) == nullptr);
}

void clients_cannot_speak_for_others(test::Context& ctx) {
  Rig rig;
  Client& honest = rig.add_client();
  rig.frames(5);
  // A hand-made client: says Hello, then sends commands pretending to be the
  // honest player, plus a server-only command.
  LoopbackNet raw(rig.hub, 60);
  std::vector<std::uint8_t> bytes;
  const std::uint64_t session = raw_join(rig, raw);
  CHECK(session != 0);
  const PlayerId cheater = static_cast<PlayerId>(honest.player_id() == 0 ? 1 : 0);
  CHECK(rig.server_player(cheater) == nullptr);  // a Hello alone creates no entity
  const float honest_x = rig.server_player(honest.player_id())->position.x;
  const float cheater_spawn_x = spawn_point_for(rig.server->world().level, cheater).x;

  InputFrame frame;
  frame.sequence = 1;
  frame.commands.push(make_command(CommandType::kPlayerLeave, honest.player_id()));
  frame.commands.push(make_command(CommandType::kMove, honest.player_id(), Vec3(1.0f, 0.0f, 0.0f)));
  const InputFrame* frames[1] = {&frame};
  write_input(bytes, session, kFullSnapshot, frames, 1);
  raw.send(Address(kLocalhostIp, 1), bytes.data(), bytes.size());
  rig.frames(20);

  CHECK(rig.server_player(honest.player_id()) != nullptr);  // not kicked
  CHECK_NEAR(rig.server_player(honest.player_id())->position.x, honest_x, 1e-6);
  const Entity* sender = rig.server_player(cheater);
  CHECK(sender != nullptr);  // the first Input made it a player...
  if (sender != nullptr) CHECK(sender->position.x > cheater_spawn_x + 1.0f);  // ...and the move applied to it
}

void a_forged_hello_gets_no_player_and_no_snapshots(test::Context& ctx) {
  Rig rig;
  LoopbackNet victim(rig.hub, 80);
  std::vector<std::uint8_t> bytes;
  write_hello(bytes, 0);
  // Pretend the Hello came from the victim (a spoofed source address).
  rig.hub.send(victim.local_address(), Address(kLocalhostIp, 1), bytes.data(), bytes.size());
  rig.frames(60);

  CHECK(rig.server->client_count() == 0);
  CHECK(!player_entity(rig.server->world(), 0).is_valid());
  std::size_t datagrams = 0;
  std::size_t largest = 0;
  std::uint8_t buffer[kMaxDatagramSize];
  Address from;
  for (std::size_t size = victim.receive(from, buffer, sizeof(buffer)); size > 0;
       size = victim.receive(from, buffer, sizeof(buffer))) {
    ++datagrams;
    if (size > largest) largest = size;
  }
  CHECK(datagrams == 1);           // just the Challenge, no snapshot stream
  CHECK(largest <= bytes.size());  // never more bytes than the Hello: no amplification
  CHECK(rig.server->reserved_slots() == 0);
  CHECK(rig.server->challenges_sent() == 1);

  // A forged Hello holds no slot, so even a one-player server stays open.
  ServerConfig one_slot;
  one_slot.max_players = 1;
  Rig small(one_slot);
  small.hub.send(Address(kLocalhostIp, 81), Address(kLocalhostIp, 1), bytes.data(), bytes.size());
  small.frame();
  CHECK(small.server->reserved_slots() == 0);
  Client& real = small.add_client();
  small.frames(10);
  CHECK(real.state() == ClientState::kConnected);
}

void reconnect_spam_never_loses_a_join_or_leave(test::Context& ctx) {
  ServerConfig config;  // limits out of the way: this test is about join/leave bookkeeping
  config.hello_replies_per_address = RateLimit{1.0e6, 1.0e6};
  config.hello_replies_total = RateLimit{1.0e6, 1.0e6};
  config.client_datagrams = RateLimit{1.0e6, 1.0e6};
  Rig rig(config);
  LoopbackNet raw(rig.hub, 90);
  std::vector<std::uint8_t> hello, input, bye;
  const std::uint64_t cookie = ask_for_cookie(rig, raw);
  CHECK(cookie != 0);
  write_hello(hello, cookie);
  const std::uint64_t session = make_session_token(rig.secret, raw.local_address());
  write_bye(bye, session);
  InputFrame frame;
  frame.sequence = 1;
  const InputFrame* frames[1] = {&frame};
  write_input(input, session, kFullSnapshot, frames, 1);
  const Address server(kLocalhostIp, 1);
  // Hundreds of join/leave cycles inside one poll.
  for (int i = 0; i < 300; ++i) {
    raw.send(server, hello.data(), hello.size());
    raw.send(server, input.data(), input.size());
    raw.send(server, bye.data(), bye.size());
  }
  raw.send(server, hello.data(), hello.size());
  raw.send(server, input.data(), input.size());  // ends connected
  rig.frames(2);
  int players = 0;
  for (const Entity& entity : rig.server->world().entities) {
    if (entity.alive && entity.player != kNoPlayer) ++players;
  }
  CHECK(players == 1);  // exactly one entity, no ghosts
  CHECK(rig.server->client_count() == 1);
}

// Another player who moves and then stops must be drawn moving smoothly and
// stopping where the server stopped them: never past it, never jumping.
void remote_players_are_interpolated_without_overshoot(test::Context& ctx) {
  Rig rig;
  rig.hub.set_latency(0.05);
  Client& watcher = rig.add_client();
  Client& mover = rig.add_client();
  rig.frames(60);
  CHECK(watcher.state() == ClientState::kConnected && mover.state() == ClientState::kConnected);

  const EntityId mover_id = player_entity(rig.server->world(), mover.player_id());
  auto drawn_x = [&]() {
    const World& shown = watcher.display_world(0.0f);
    const Entity* e = find_entity(shown, mover_id);
    return e != nullptr ? e->position.x : -1.0f;
  };

  const float speed_per_tick = 5.0f / 60.0f;  // the hero's speed in top-down.scene
  float last = drawn_x();
  float largest_step = 0.0f;
  float furthest = last;
  rig.commands[1] = move_command(Vec2(1.0f, 0.0f));
  for (int i = 0; i < 30; ++i) {
    rig.frame();
    const float x = drawn_x();
    largest_step = std::max(largest_step, std::fabs(x - last));
    furthest = std::max(furthest, x);
    last = x;
  }
  rig.commands[1] = move_command(Vec2(0.0f, 0.0f));
  for (int i = 0; i < 60; ++i) {
    rig.frame();
    const float x = drawn_x();
    largest_step = std::max(largest_step, std::fabs(x - last));
    furthest = std::max(furthest, x);
    last = x;
  }
  const float stopped_at = rig.server_player(mover.player_id())->position.x;
  CHECK(furthest <= stopped_at + 1e-4f);                // never drawn past the real stop
  CHECK_NEAR(last, stopped_at, 1e-4);                   // and ends exactly there
  CHECK(largest_step <= speed_per_tick * 1.5f + 1e-4f);  // no jumps
  CHECK(furthest > stopped_at - 0.01f);                 // it did move all the way
}

// A scene edited while the game runs: everyone gets the new level and keeps playing.
void hot_reload_reaches_every_client(test::Context& ctx) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::u8path(SCRAPS_TEST_WORK_DIR) / "scraps_reload_test";
  std::error_code ignored;
  fs::create_directories(dir, ignored);
  std::string original;
  {
    std::ifstream in(std::string(SCRAPS_SANDBOX_SCENE_DIR) + "/top-down.scene", std::ios::binary);
    original.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    std::ofstream(dir / "top-down.scene", std::ios::binary) << original;
  }

  Rig rig(ServerConfig(), "top-down", dir.u8string());
  Client& a = rig.add_client(dir.u8string());
  Client& b = rig.add_client(dir.u8string());
  rig.frames(10);
  CHECK(a.state() == ClientState::kConnected && b.state() == ClientState::kConnected);
  const std::uint32_t generation_a = a.world_generation();

  // Edit: knock a hole in the north wall (row 0 of layer 1 is all '#').
  std::string edited = original;
  const std::size_t wall_row = edited.find("########################");
  CHECK(wall_row != std::string::npos);
  edited[wall_row + 5] = '.';
  { std::ofstream(dir / "top-down.scene", std::ios::binary) << edited; }
  Scene scene;
  std::string error;
  CHECK(load_scene(dir.u8string(), "top-down", scene, error));

  // Client b misses the Welcome that announces the reload (all packets lost
  // for a moment); it must notice from the snapshots and ask again.
  rig.hub.set_drop_rate(1.0);
  const Tick tick_before = rig.server->world().tick;
  rig.server->reload(make_world(scene, movement_rules_for(scene.level.perspective)), scene.name, scene.content_hash);
  rig.hub.set_drop_rate(0.0);
  rig.frames(30);

  CHECK(rig.server->world().tick >= tick_before);  // ticks never go backwards
  CHECK(rig.server->world().level.grid.get(5, 0, 1) == kEmptyTile);
  for (Client* client : {&a, &b}) {
    CHECK(client->state() == ClientState::kConnected);
    CHECK(client->current_world().level.grid.get(5, 0, 1) == kEmptyTile);  // the new level
    CHECK(Rig::client_player(*client) != nullptr);                         // still playing
  }
  CHECK(a.world_generation() == generation_a + 1);
  CHECK(rig.server->client_count() == 2);

  // Deltas resume in the new world, for b too, which acked an old-world
  // snapshot for a while.
  rig.frames(30);
  for (Client* client : {&a, &b}) {
    const std::uint64_t deltas = client->delta_snapshots_received();
    rig.frames(10);
    CHECK(client->delta_snapshots_received() >= deltas + 9);
    CHECK(client->current_world().entities.size() == rig.server->world().entities.size());
    const Entity* mine = Rig::client_player(*client);
    const Entity* truth = rig.server_player(client->player_id());
    CHECK(mine != nullptr && truth != nullptr);
    if (mine != nullptr && truth != nullptr) CHECK_NEAR(mine->position.x, truth->position.x, 1e-4);
  }
  fs::remove_all(dir, ignored);
}

void sequence_numbers_wrap(test::Context& ctx) {
  CHECK(sequence_newer(1, 0));
  CHECK(sequence_newer(0, 0xFFFFFFFFu));
  CHECK(sequence_newer(5, 0xFFFFFFF0u));
  CHECK(!sequence_newer(5, 5));
  CHECK(!sequence_newer(4, 5));
  CHECK(!sequence_newer(0xFFFFFFFFu, 0));
}

void snapshots_reassemble_from_fragments(test::Context& ctx) {
  std::vector<std::uint8_t> payload(kFragmentPayloadSize * 2 + 100);
  for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(i * 7);
  auto fragment = [&](std::uint32_t id, std::uint16_t index, FragmentHeader& header, std::size_t& size) {
    header.snapshot_id = id;
    header.index = index;
    header.count = 3;
    header.ack_sequence = 42;
    size = index == 2 ? 100 : kFragmentPayloadSize;
    return payload.data() + index * kFragmentPayloadSize;
  };

  SnapshotAssembler assembler;
  FragmentHeader header;
  std::size_t size = 0;
  const std::uint8_t* data = fragment(1, 2, header, size);
  CHECK(!assembler.add(header, data, size));  // out of order is fine
  data = fragment(1, 0, header, size);
  CHECK(!assembler.add(header, data, size));
  CHECK(!assembler.add(header, data, size));  // duplicate ignored
  data = fragment(1, 1, header, size);
  CHECK(assembler.add(header, data, size));  // complete
  CHECK(assembler.size() == payload.size());
  CHECK(assembler.ack_sequence() == 42);
  CHECK(std::vector<std::uint8_t>(assembler.data(), assembler.data() + assembler.size()) == payload);

  data = fragment(1, 0, header, size);
  CHECK(!assembler.add(header, data, size));  // the finished snapshot does not restart

  // A newer snapshot abandons a partial one; the old one's fragments are then ignored.
  data = fragment(3, 0, header, size);
  CHECK(!assembler.add(header, data, size));
  data = fragment(4, 0, header, size);
  CHECK(!assembler.add(header, data, size));
  data = fragment(3, 1, header, size);
  CHECK(!assembler.add(header, data, size));
  data = fragment(4, 1, header, size);
  CHECK(!assembler.add(header, data, size));
  data = fragment(4, 2, header, size);
  CHECK(assembler.add(header, data, size));
}

void garbage_datagrams_are_ignored(test::Context& ctx) {
  Rig rig;
  Client& client = rig.add_client();
  rig.frames(5);
  LoopbackNet raw(rig.hub, 70);
  std::uint32_t seed = 99;
  for (int i = 0; i < 3000; ++i) {
    std::vector<std::uint8_t> noise(1 + (seed >> 20) % 300);
    for (std::uint8_t& b : noise) {
      seed = seed * 1664525u + 1013904223u;
      b = static_cast<std::uint8_t>(seed >> 24);
    }
    if (i % 2 == 0 && noise.size() >= 5) {  // half of them get a valid header
      noise[0] = 0x53;
      noise[1] = 0x43;
      noise[2] = 0x52;
      noise[3] = 0x50;
      noise[4] = static_cast<std::uint8_t>(1 + i % 6);
    }
    raw.send(Address(kLocalhostIp, 1), noise.data(), noise.size());
    raw.send(Address(kLocalhostIp, 100), noise.data(), noise.size());  // at the client too
    if (i % 100 == 0) rig.frame();
  }
  rig.frames(5);
  CHECK(client.state() == ClientState::kConnected);
  CHECK(rig.server->client_count() >= 1);
}

// ---------------------------------------------------------------- hardening

void siphash_matches_the_reference(test::Context& ctx) {
  // The test vector from the SipHash paper: key 00..0f, message 00..0e.
  std::uint8_t message[15];
  for (int i = 0; i < 15; ++i) message[i] = static_cast<std::uint8_t>(i);
  CHECK(siphash24(0x0706050403020100ull, 0x0F0E0D0C0B0A0908ull, message, sizeof(message)) == 0xA129CA6149BE45E5ull);
}

void cookie_is_bound_to_address_and_time(test::Context& ctx) {
  Rig rig;
  LoopbackNet a(rig.hub, Address(0x0A000101u, 500));
  LoopbackNet same_ip_other_port(rig.hub, Address(0x0A000101u, 501));
  const std::uint64_t cookie = ask_for_cookie(rig, a);
  CHECK(cookie != 0);

  std::vector<std::uint8_t> bytes;
  write_hello(bytes, cookie);
  same_ip_other_port.send(kServerAddress, bytes.data(), bytes.size());  // someone else's cookie
  rig.frame();
  CHECK(drain(same_ip_other_port) != 0);  // a fresh Challenge, not a Welcome
  CHECK(rig.server->reserved_slots() == 0);

  rig.frames(5 * 60);  // 5 s: past both cookie windows
  a.send(kServerAddress, bytes.data(), bytes.size());
  rig.frame();
  const std::uint64_t fresh = drain(a);
  CHECK(fresh != 0 && fresh != cookie);  // expired: challenged again
  CHECK(rig.server->reserved_slots() == 0);

  write_hello(bytes, fresh);
  a.send(kServerAddress, bytes.data(), bytes.size());
  rig.frame();
  CHECK(rig.server->reserved_slots() == 1);  // a fresh cookie from the right address gets a slot
}

void hello_flood_is_rate_limited(test::Context& ctx) {
  Rig rig;
  LoopbackNet flooder(rig.hub, Address(0x0A000201u, 600));
  std::vector<std::uint8_t> bytes;
  write_hello(bytes, 0);
  Client& real = rig.add_client();
  std::size_t replies = 0;
  for (int f = 0; f < 60; ++f) {  // 10,020 Hellos in one second
    for (int i = 0; i < 167; ++i) flooder.send(kServerAddress, bytes.data(), bytes.size());
    rig.frame();
    drain(flooder, &replies);
  }
  CHECK(replies <= 16);  // a burst of 10, then 5 per second
  CHECK(rig.server->hellos_rate_limited() > 9900);
  CHECK(real.state() == ClientState::kConnected);  // another address still got in
}

void many_addresses_share_the_global_limit(test::Context& ctx) {
  Rig rig;
  std::vector<std::uint8_t> bytes;
  write_hello(bytes, 0);
  for (std::uint32_t i = 0; i < 1000; ++i) {  // 1,000 (faked) addresses, one Hello each, at once
    rig.hub.send(Address(0x0B000000u + i, 700), kServerAddress, bytes.data(), bytes.size());
  }
  rig.frame();
  CHECK(rig.server->challenges_sent() <= 200);
  CHECK(rig.server->hellos_rate_limited() >= 800);
  CHECK(rig.server->reserved_slots() == 0);
}

void input_flood_is_capped_per_client(test::Context& ctx) {
  Rig rig;
  Client& honest = rig.add_client();
  rig.frames(10);
  LoopbackNet raw(rig.hub, Address(0x0A000301u, 800));
  const std::uint64_t session = raw_join(rig, raw);
  CHECK(session != 0);
  InputFrame frame;
  frame.sequence = 1;
  const InputFrame* frames[1] = {&frame};
  std::vector<std::uint8_t> input;
  rig.commands[0] = move_command(Vec2(1.0f, 0.0f));
  const float start_x = rig.server_player(honest.player_id())->position.x;
  for (int f = 0; f < 60; ++f) {  // 100 datagrams per tick for a second
    for (int i = 0; i < 100; ++i) {
      ++frame.sequence;
      write_input(input, session, kFullSnapshot, frames, 1);
      raw.send(kServerAddress, input.data(), input.size());
    }
    rig.frame();
    drain(raw);
  }
  CHECK(rig.server->client_datagrams_dropped() > 6000 - 600);  // ~8 per tick get through
  CHECK(rig.server->client_count() == 2);
  CHECK(rig.server_player(honest.player_id())->position.x > start_x + 3.0f);  // the honest player kept playing
}

void a_vanished_players_slot_cannot_be_kept_alive(test::Context& ctx) {
  Rig rig;
  LoopbackNet raw(rig.hub, Address(0x0A000501u, 900));
  const std::uint64_t session = raw_join(rig, raw);
  CHECK(session != 0);
  InputFrame frame;
  frame.sequence = 1;
  const InputFrame* frames[1] = {&frame};
  std::vector<std::uint8_t> input;
  write_input(input, session, kFullSnapshot, frames, 1);
  raw.send(kServerAddress, input.data(), input.size());
  rig.frames(5);
  CHECK(rig.server->client_count() == 1);

  // The player vanishes. Someone who knows its address keeps sending a
  // Hello and an Input (without the session token) "from" it.
  std::vector<std::uint8_t> hello;
  write_hello(hello, 0);
  std::vector<std::uint8_t> forged;
  write_input(forged, 0, kFullSnapshot, frames, 1);
  std::size_t largest = 0;
  for (int second = 0; second < 8; ++second) {
    rig.hub.send(raw.local_address(), kServerAddress, hello.data(), hello.size());
    rig.hub.send(raw.local_address(), kServerAddress, forged.data(), forged.size());
    rig.frames(60);
    std::uint64_t welcome_session = 0;
    drain(raw, nullptr, &largest, &welcome_session);
    if (second >= 1) CHECK(welcome_session == 0);  // never another Welcome without a cookie
  }
  CHECK(rig.server->client_count() == 0);  // timed out: the slot was not kept alive
  CHECK(rig.server->forged_datagrams() >= 4);  // counted while the slot existed (about 5 s)
}

void forged_input_and_bye_are_ignored(test::Context& ctx) {
  Rig rig;
  Client& honest = rig.add_client();
  rig.frames(10);
  const Address victim = rig.nets[0]->local_address();
  const float x = rig.server_player(honest.player_id())->position.x;
  std::vector<std::uint8_t> bye;
  write_bye(bye, 12345);  // a guessed token
  InputFrame frame;
  frame.sequence = 100000;
  frame.commands.push(make_command(CommandType::kMove, 0, Vec3(1.0f, 0.0f, 0.0f)));
  const InputFrame* frames[1] = {&frame};
  std::vector<std::uint8_t> input;
  write_input(input, 0, kFullSnapshot, frames, 1);
  for (int i = 0; i < 20; ++i) {
    rig.hub.send(victim, kServerAddress, input.data(), input.size());
    rig.hub.send(victim, kServerAddress, bye.data(), bye.size());
    rig.frame();
  }
  CHECK(rig.server->client_count() == 1);  // not kicked
  CHECK(honest.state() == ClientState::kConnected);
  CHECK_NEAR(rig.server_player(honest.player_id())->position.x, x, 1e-6);  // not moved
  CHECK(rig.server->forged_datagrams() == 40);
}

void cookie_from_the_previous_window_is_accepted(test::Context& ctx) {
  Rig rig;
  LoopbackNet raw(rig.hub, Address(0x0A000601u, 950));
  rig.now = 3.97;  // the Challenge comes at 3.987 s (window 1)...
  const std::uint64_t cookie = ask_for_cookie(rig, raw);
  CHECK(cookie != 0);
  CHECK(cookie_window(rig.now) == 1);
  std::vector<std::uint8_t> bytes;
  write_hello(bytes, cookie);
  raw.send(kServerAddress, bytes.data(), bytes.size());
  rig.frame();  // ...and is echoed at 4.003 s (window 2)
  CHECK(cookie_window(rig.now) == 2);
  CHECK(rig.server->reserved_slots() == 1);
}

void a_server_without_a_secret_lets_nobody_in(test::Context& ctx) {
  Rig rig(ServerConfig(), "top-down", SCRAPS_SANDBOX_SCENE_DIR, true);
  CHECK(!rig.server->accepts_players());
  Client& client = rig.add_client();
  rig.frames(60);
  CHECK(client.state() == ClientState::kConnecting);
  CHECK(rig.server->reserved_slots() == 0 && rig.server->challenges_sent() == 0);
}

void a_household_behind_one_address_can_join(test::Context& ctx) {
  Rig rig;
  std::vector<Client*> family;
  for (int i = 0; i < 6; ++i) family.push_back(&rig.add_client(SCRAPS_SANDBOX_SCENE_DIR, 0x0A000401u));
  rig.frames(3 * 60);
  for (Client* client : family) CHECK(client->state() == ClientState::kConnected);
  CHECK(rig.server->client_count() == 6);
}

// ---------------------------------------------------------------- delta snapshots

// Every slot of the client's world matches the server's: same entities, same
// places. (The client is a tick or two ahead in time, so only things that
// have stopped are compared exactly.)
int mismatched_slots(const World& client, const World& server) {
  if (client.entities.size() != server.entities.size()) return -1;
  int mismatched = 0;
  for (std::size_t i = 0; i < server.entities.size(); ++i) {
    const Entity& a = client.entities[i];
    const Entity& b = server.entities[i];
    if (a.alive != b.alive || a.generation != b.generation) {
      ++mismatched;
    } else if (b.alive && (b.flags & kEntityMoves) != 0 && b.velocity.x == 0.0f && b.velocity.y == 0.0f) {
      if (std::fabs(a.position.x - b.position.x) > 1e-4f || std::fabs(a.position.y - b.position.y) > 1e-4f) {
        ++mismatched;
      }
    }
  }
  return mismatched;
}

void snapshot_acks_survive_reorder_and_wrap(test::Context& ctx) {
  CHECK(updated_snapshot_ack(kFullSnapshot, 5) == 5);
  CHECK(updated_snapshot_ack(5, 7) == 7);
  CHECK(updated_snapshot_ack(7, 5) == 7);                                // late, older Input
  CHECK(updated_snapshot_ack(7, kFullSnapshot) == kFullSnapshot);        // "I hold nothing"
  CHECK(updated_snapshot_ack(kFullSnapshot, 0x80000005u) == 0x80000005u);  // past 2^31
  CHECK(updated_snapshot_ack(0x80000005u, 0x80000009u) == 0x80000009u);
  CHECK(updated_snapshot_ack(0xFFFFFFFFu, 1u) == 1u);                      // wrap (0 is skipped)
  CHECK(updated_snapshot_ack(1u, 0xFFFFFFFFu) == 1u);
}

void deltas_survive_loss_and_reorder(test::Context& ctx) {
  Rig rig;
  rig.hub.set_drop_rate(0.2);
  rig.hub.set_latency(0.05);
  rig.hub.set_jitter(0.04);  // more than two snapshot intervals: datagrams overtake each other
  Client& a = rig.add_client();
  Client& b = rig.add_client();
  rig.frames(90);
  CHECK(a.state() == ClientState::kConnected && b.state() == ClientState::kConnected);
  rig.commands[0] = move_command(Vec2(1.0f, 0.0f));
  rig.commands[1] = move_command(Vec2(0.0f, 1.0f));
  rig.frames(60);
  rig.commands[0] = move_command(Vec2(0.0f, 0.0f));
  rig.commands[1] = move_command(Vec2(0.0f, 0.0f));
  rig.frames(120);

  CHECK(rig.hub.dropped() > 0);
  CHECK(rig.server->delta_snapshots_sent() > rig.server->full_snapshots_sent() * 10);
  for (Client* client : {&a, &b}) {
    CHECK(client->delta_snapshots_received() > 100);
    CHECK(mismatched_slots(client->current_world(), rig.server->world()) == 0);
    const Entity* server_side = rig.server_player(client->player_id());
    const Entity* client_side = Rig::client_player(*client);
    CHECK(server_side != nullptr && client_side != nullptr);
    if (server_side == nullptr || client_side == nullptr) continue;
    CHECK_NEAR(client_side->position.x, server_side->position.x, 1e-4);
    CHECK_NEAR(client_side->position.y, server_side->position.y, 1e-4);
  }
  // The players really moved, so the deltas carried real changes.
  CHECK(rig.server_player(a.player_id())->position.x > 4.0f);
}

void full_snapshot_when_baseline_is_too_old(test::Context& ctx) {
  Rig rig;
  Client& client = rig.add_client();
  rig.frames(30);
  CHECK(client.state() == ClientState::kConnected);
  const std::uint64_t full_before = rig.server->full_snapshots_sent();
  CHECK(full_before >= 1);  // the first snapshot has no baseline
  CHECK(client.delta_snapshots_received() > 0);

  // The client goes quiet for 1.5 s (90 snapshots, far past the 32 kept)
  // without timing out. The server keeps sending deltas against the last
  // snapshot it heard of, then has to fall back to full ones.
  for (int i = 0; i < 90; ++i) {
    rig.now += kFrame;
    rig.hub.set_now(rig.now);
    rig.server->poll(rig.now);
    rig.server->tick();
  }
  CHECK(rig.server->full_snapshots_sent() > full_before + 30);
  CHECK(rig.server->client_count() == 1);

  // Back again: it catches up from a full snapshot, then deltas resume.
  const std::uint64_t full_received = client.full_snapshots_received();
  rig.frames(30);
  CHECK(client.full_snapshots_received() > full_received);
  const std::uint64_t deltas = client.delta_snapshots_received();
  const std::uint64_t full_sent = rig.server->full_snapshots_sent();
  rig.frames(30);
  CHECK(client.delta_snapshots_received() >= deltas + 29);
  CHECK(rig.server->full_snapshots_sent() == full_sent);  // no more full ones
  CHECK(mismatched_slots(client.current_world(), rig.server->world()) == 0);
}

void idle_scene_snapshots_are_tiny(test::Context& ctx) {
  // Isometric: players and coins, nothing that moves by itself.
  Rig rig(ServerConfig(), "isometric");
  Client& a = rig.add_client();
  Client& b = rig.add_client();
  rig.frames(60);
  CHECK(a.state() == ClientState::kConnected && b.state() == ClientState::kConnected);
  for (Client* client : {&a, &b}) {
    const std::size_t size = rig.server->last_snapshot_size(client->player_id());
    CHECK(size > 0 && size < 64);
  }
  // One player walking costs a few bytes more, not a full snapshot.
  rig.commands[0] = move_command(Vec2(1.0f, 0.0f));
  rig.frames(5);
  CHECK(rig.server->last_snapshot_size(b.player_id()) < 96);

  // Top-down has 8 critters that never stop: small, but not header-only.
  Rig garden;
  Client& c = garden.add_client();
  garden.frames(60);
  const std::size_t garden_size = garden.server->last_snapshot_size(c.player_id());
  std::printf("    snapshot sizes: isometric idle %lu B, top-down %lu B\n",
              static_cast<unsigned long>(rig.server->last_snapshot_size(a.player_id())),
              static_cast<unsigned long>(garden_size));
  CHECK(garden_size > 0 && garden_size < 256);
}

}  // namespace

void register_net_tests(test::Registry& registry) {
  registry.add("net.two_clients_share_one_world", two_clients_share_one_world);
  registry.add("net.inputs_move_the_authoritative_player", inputs_move_the_authoritative_player);
  registry.add("net.prediction_hides_latency", prediction_hides_latency);
  registry.add("net.lossy_network_still_converges", lossy_network_still_converges);
  registry.add("net.a_full_server_rejects", a_full_server_rejects);
  registry.add("net.wrong_protocol_version_is_rejected", wrong_protocol_version_is_rejected);
  registry.add("net.a_different_scene_file_is_refused", a_different_scene_file_is_refused);
  registry.add("net.silence_times_out_both_ways", silence_times_out_both_ways);
  registry.add("net.bye_removes_the_player", bye_removes_the_player);
  registry.add("net.clients_cannot_speak_for_others", clients_cannot_speak_for_others);
  registry.add("net.a_forged_hello_gets_no_player_and_no_snapshots", a_forged_hello_gets_no_player_and_no_snapshots);
  registry.add("net.reconnect_spam_never_loses_a_join_or_leave", reconnect_spam_never_loses_a_join_or_leave);
  registry.add("net.remote_players_are_interpolated_without_overshoot",
               remote_players_are_interpolated_without_overshoot);
  registry.add("net.hot_reload_reaches_every_client", hot_reload_reaches_every_client);
  registry.add("net.sequence_numbers_wrap", sequence_numbers_wrap);
  registry.add("net.snapshots_reassemble_from_fragments", snapshots_reassemble_from_fragments);
  registry.add("net.garbage_datagrams_are_ignored", garbage_datagrams_are_ignored);
  registry.add("net.snapshot_acks_survive_reorder_and_wrap", snapshot_acks_survive_reorder_and_wrap);
  registry.add("net.siphash_matches_the_reference", siphash_matches_the_reference);
  registry.add("net.cookie_is_bound_to_address_and_time", cookie_is_bound_to_address_and_time);
  registry.add("net.hello_flood_is_rate_limited", hello_flood_is_rate_limited);
  registry.add("net.many_addresses_share_the_global_limit", many_addresses_share_the_global_limit);
  registry.add("net.input_flood_is_capped_per_client", input_flood_is_capped_per_client);
  registry.add("net.a_household_behind_one_address_can_join", a_household_behind_one_address_can_join);
  registry.add("net.a_vanished_players_slot_cannot_be_kept_alive", a_vanished_players_slot_cannot_be_kept_alive);
  registry.add("net.forged_input_and_bye_are_ignored", forged_input_and_bye_are_ignored);
  registry.add("net.cookie_from_the_previous_window_is_accepted", cookie_from_the_previous_window_is_accepted);
  registry.add("net.a_server_without_a_secret_lets_nobody_in", a_server_without_a_secret_lets_nobody_in);
  registry.add("net.deltas_survive_loss_and_reorder", deltas_survive_loss_and_reorder);
  registry.add("net.full_snapshot_when_baseline_is_too_old", full_snapshot_when_baseline_is_too_old);
  registry.add("net.idle_scene_snapshots_are_tiny", idle_scene_snapshots_are_tiny);
}
