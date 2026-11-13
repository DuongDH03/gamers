# A second game, as small as a game can be: the engine's own rules and one
# scene. It proves that scraps_add_game() works for a game other than the
# sandbox, and that the apps run whatever game they are built for:
#   - tests build scraps_client_tiny for it (tests/CMakeLists.txt);
#   - -DSCRAPS_GAME=tiny builds the real apps for it.
scraps_add_game(tiny
  SOURCES tiny/tiny.cpp
  SCENES scenes)
