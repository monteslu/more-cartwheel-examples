# Party Pack: a party game for one to four players on one shared screen
# (working title). A hub with a board, a mini-game framework and nine
# mini-games.
#   partypack       the game
#   partypack_auto  four CPU players play a tournament by themselves (tests, screenshots)
file(GLOB PP_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/partypack.txt)
cw_add_app(partypack SOURCES ${PP_SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(partypack_auto SOURCES ${PP_SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(partypack_auto PRIVATE PP_AUTOPILOT=1)
add_dependencies(partypack_auto_pack partypack_pack)
