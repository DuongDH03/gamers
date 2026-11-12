#include "harness.h"

void register_core_tests(test::Registry& registry);
void register_scene_tests(test::Registry& registry);
void register_view_tests(test::Registry& registry);
void register_net_tests(test::Registry& registry);
void register_sandbox_tests(test::Registry& registry);
void register_udp_tests(test::Registry& registry);
void register_game_tests(test::Registry& registry);

// Usage: scraps_tests [name-prefix]   e.g. "net." runs only the net tests.
int main(int argc, char** argv) {
  test::Registry registry;
  register_core_tests(registry);
  register_scene_tests(registry);
  register_view_tests(registry);
  register_net_tests(registry);
  register_sandbox_tests(registry);
  register_udp_tests(registry);
  register_game_tests(registry);
  return registry.run(argc > 1 ? argv[1] : "") == 0 ? 0 : 1;
}
