# hordeshooter (working title): a co-op twin-stick survival shooter against a
# crowd of thousands, one shared camera.
#   hordeshooter       the game
#   hordeshooter_auto  bots play it (for hosts that send no input)
#   hordeshooter_soak  the rules and the bots with no window and no GPU (native only)
set(SRC main.c sim.c bot.c view.c fx.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/hordeshooter.txt)
cw_add_app(hordeshooter SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(hordeshooter_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(hordeshooter_auto PRIVATE HORDE_AUTOPILOT=1)
add_dependencies(hordeshooter_auto_pack hordeshooter_pack)
if(NOT CW_CART)
  add_executable(hordeshooter_soak soak.c sim.c bot.c)
  target_link_libraries(hordeshooter_soak PRIVATE cartwheel)
endif()
