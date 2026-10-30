# hordeshooter (working title): a co-op twin-stick survival shooter against a
# crowd of thousands, one shared camera.
#   hordeshooter       the game
#   hordeshooter_auto  bots play it (for hosts that send no input)
#   hordeshooter_soak  the rules and the bots with no window and no GPU (native only)
set(SRC main.c sim.c bot.c view.c fx.c floors.c tools/levelview/levelbuild.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/hordeshooter.txt)
cw_add_app(hordeshooter SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(hordeshooter_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(hordeshooter_auto PRIVATE HORDE_AUTOPILOT=1)
foreach(t hordeshooter hordeshooter_auto)
  target_include_directories(${t} PRIVATE tools/levelview)
endforeach()
add_dependencies(hordeshooter_auto_pack hordeshooter_pack)
if(NOT CW_CART)
  add_executable(hordeshooter_soak soak.c sim.c bot.c floors.c tools/levelview/levelbuild.c)
  target_link_libraries(hordeshooter_soak PRIVATE cartwheel)
  target_include_directories(hordeshooter_soak PRIVATE tools/levelview)
  target_compile_definitions(hordeshooter_soak PRIVATE HORDE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets") # the floors' .cwlevel files
endif()
# hordeshooter_bench: the bots at the run's wave 24 (floor 5, wave 4: the difficulty of the old wave 14, about
# 2000 enemies) with three players from the first frame, the
# late-wave scene the perf gate times (carts read no environment, so it is built in)
cw_add_app(hordeshooter_bench SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(hordeshooter_bench PRIVATE HORDE_AUTOPILOT=1 HORDE_START_WAVE=24 HORDE_AUTO_PLAYERS=3)
target_include_directories(hordeshooter_bench PRIVATE tools/levelview)
add_dependencies(hordeshooter_bench_pack hordeshooter_pack)
include(tools/levelview/levelview.cmake)
