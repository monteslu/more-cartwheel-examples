# Brawler: a 1 to 4 player co-op street brawler on one shared scrolling screen.
#   brawler       the game
#   brawler_auto  bots play it (for hosts that send no input, and the gates)
set(SRC src/main.c src/sim.c src/level.c src/fx.c src/ui.c src/secondary.c src/rag.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/brawler.txt)
cw_add_app(brawler SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(brawler_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(brawler_auto PRIVATE BRAWLER_AUTOPILOT=1)
add_dependencies(brawler_auto_pack brawler_pack)
