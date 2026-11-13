# One scraps_add_game() call per game (cmake/ScrapsGame.cmake): the static
# library, the hot-reload module and what the apps link. The whole scenes
# folder (scenes, art, sounds, music) ships in a release.

scraps_add_game(sandbox
  SOURCES sandbox/sandbox.cpp sandbox/sandbox_api.cpp
  SCENES sandbox/scenes
  DEFAULT_SCENE isometric
  README sandbox/README.txt)
