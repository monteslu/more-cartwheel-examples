# jetski: a wave racing game on open water, 1 or 2 players (split screen) and computer racers.
#   jetski       the game
#   jetski_auto  a computer rider plays it (for hosts that send no input, and the gates)
set(SRC src/main.c src/sim.c src/view.c src/rider.c src/fx.c src/hud.c src/audio.c src/menu.c src/weather.c src/lens.c src/life.c src/radio.c src/podium.c src/courses/all.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/jetski.txt)
configure_file(project.cwproj ${CMAKE_CURRENT_SOURCE_DIR}/assets/project.cwproj COPYONLY)
cw_add_app(jetski SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(jetski_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(jetski_auto PRIVATE JETSKI_AUTOPILOT=1)
add_dependencies(jetski_auto_pack jetski_pack)
target_compile_options(jetski PRIVATE -Wall)
