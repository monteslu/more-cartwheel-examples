# coopplatformer (working title): a co-op side-scrolling platformer with a connected world.
#   coopplatformer       the game
#   coopplatformer_auto  plays itself (for hosts that send no input)
set(SRC main.c sim.c worldgen.c world.c chars.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/coopplatformer.txt)
cw_add_app(coopplatformer SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(coopplatformer_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(coopplatformer_auto PRIVATE COOP_AUTOPILOT=1)
add_dependencies(coopplatformer_auto_pack coopplatformer_pack)
