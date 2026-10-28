# Sky Fighter: an arcade jet combat game, one player, eight missions over its own terrains.
#   skyfighter   the game
# Three program tables: the engine sample states its scenes share, the terrain demo's, and its own.
cw_add_app(skyfighter SOURCES src/main.c PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt
  PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/skyfighter.txt
           ${CMAKE_CURRENT_SOURCE_DIR}/programs/cw_samples.txt
           ${CMAKE_CURRENT_SOURCE_DIR}/programs/terrain_demo.txt)
