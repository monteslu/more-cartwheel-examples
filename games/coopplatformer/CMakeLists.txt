# coopplatformer (working title): a co-op side-scrolling platformer with a connected world.
#   coopplatformer       the game
#   coopplatformer_auto  plays itself (for hosts that send no input)
set(SRC main.c sim.c worldgen.c world.c chars.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/coopplatformer.txt)
cw_add_app(coopplatformer SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(coopplatformer_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(coopplatformer_auto PRIVATE COOP_AUTOPILOT=1)
add_dependencies(coopplatformer_auto_pack coopplatformer_pack)
# gate variants: the same autopilot with a start the cart hosts (which have no environment) can run
#   _auto4: four players from the start; _auto_boss: two players at the boss arena with every ability
foreach(variant auto4 auto_boss)
  cw_add_app(coopplatformer_${variant} SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
  add_dependencies(coopplatformer_${variant}_pack coopplatformer_pack)
endforeach()
target_compile_definitions(coopplatformer_auto4 PRIVATE COOP_AUTOPILOT=1 COOP_DEF_PLAYERS=4)
target_compile_definitions(coopplatformer_auto_boss PRIVATE COOP_AUTOPILOT=1 COOP_DEF_PLAYERS=2 COOP_DEF_START_X=246 COOP_DEF_ABILITIES=7)
