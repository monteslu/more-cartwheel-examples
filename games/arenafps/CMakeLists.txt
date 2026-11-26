# arenafps (working title): a split screen first-person shooter with bots.
#   arenafps       the game
#   arenafps_auto  bots only: a match plays itself (for hosts that send no input)
file(GLOB SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/arenafps.txt)
cw_add_app(arenafps SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/roots.txt)
cw_add_app(arenafps_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/roots.txt)
target_compile_definitions(arenafps_auto PRIVATE ARENA_AUTOPILOT=1)
add_dependencies(arenafps_auto_pack arenafps_pack)
#   arenafps_navcheck  a tool: the game's arena code builds a map and its navmesh, and every command post must reach every
#                      other (tools/navcheck.c; tools/battle-check.mjs runs it)
cw_add_app(arenafps_navcheck SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/arena.c ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx.c ${CMAKE_CURRENT_SOURCE_DIR}/tools/navcheck.c PACK ${CMAKE_CURRENT_SOURCE_DIR}/roots.txt)
add_dependencies(arenafps_navcheck_pack arenafps_pack)
