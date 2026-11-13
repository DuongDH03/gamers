#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "backends/udp/udp_net.h"
#include "core/scene.h"
#include "harness.h"
#include "net/client.h"
#include "net/server.h"
#include "sandbox/sandbox.h"
#include "views/view_factory.h"

using namespace scraps;

namespace {

// The real thing: a server and a client over actual UDP sockets on this
// machine (127.0.0.1 only, so no firewall prompt).
void a_client_joins_over_real_udp(test::Context& ctx) {
  std::string error;
  std::unique_ptr<udp::UdpNet> server_net = udp::UdpNet::open(kLocalhostIp, 0, error);
  std::unique_ptr<udp::UdpNet> client_net = udp::UdpNet::open(kLocalhostIp, 0, error);
  CHECK(server_net != nullptr && client_net != nullptr);
  if (!server_net || !client_net) {
    std::printf("    %s\n", error.c_str());
    return;
  }
  CHECK(server_net->local_address().port != 0);

  Scene scene;
  CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, "isometric", scene, error));
  World world = make_world(scene, movement_rules_for(scene.level.perspective));
  ServerConfig server_config;
  server_config.cookie_secret = CookieSecret{0x5EC2E7ull, 0xC0FFEEull};
  Server server(*server_net, std::move(world), &sandbox::simulate, scene.name, scene.content_hash, server_config);
  ClientConfig config;
  config.scene_directory = SCRAPS_SANDBOX_SCENE_DIR;
  Client client(*client_net, server_net->local_address(), &sandbox::simulate, config);

  CommandList move;
  move.push(make_command(CommandType::kMove, 0, Vec3(1.0f, 0.0f, 0.0f)));
  const auto start = std::chrono::steady_clock::now();
  double now = 0.0;
  int connected_ticks = 0;
  // Generous: sleep_for(2 ms) takes ~16 ms on Windows, and a busy machine
  // (both presets testing at once) can stretch 60 ticks well past 3 s.
  while (now < 15.0 && connected_ticks < 60) {
    now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    client.poll(now);
    client.tick(move, now);
    server.poll(now);
    server.tick();
    if (client.state() == ClientState::kConnected && client.local_entity().is_valid()) ++connected_ticks;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(client.state() == ClientState::kConnected);
  CHECK(server.client_count() == 1);
  CHECK(connected_ticks == 60);
  const Entity* me = find_entity(client.current_world(), client.local_entity());
  CHECK(me != nullptr);
  if (me != nullptr) CHECK(me->position.x > scene.level.spawn_points[0].x + 0.5f);
  client.disconnect();
}

}  // namespace

void register_udp_tests(test::Registry& registry) {
  registry.add("udp.a_client_joins_over_real_udp", a_client_joins_over_real_udp);
}
