# Fighter: a one-on-one 3D-rendered fighting game with 2D rules (frame data,
# hitboxes, motion inputs), for two players on one screen or one against a CPU.
#   fighter        the game
#   fighter_auto   starts a match between two CPU fighters by itself (the gates, screenshots and the soak)
set(SRC main.c sim.c ai.c chars.c textures.c fx.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/fighter.txt)
set(PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(fighter SOURCES ${SRC} PACK ${PACK})
cw_add_app(fighter_auto SOURCES ${SRC} PACK ${PACK})
target_compile_definitions(fighter_auto PRIVATE FIGHTER_AUTOPILOT=1)
add_dependencies(fighter_auto_pack fighter_pack)
# fighter_test: two human pads with no menus (the scripted gates play player 1 with a pad script)
cw_add_app(fighter_test SOURCES ${SRC} PACK ${PACK})
target_compile_definitions(fighter_test PRIVATE FIGHTER_SCRIPTED=1)
add_dependencies(fighter_test_pack fighter_auto_pack)
# fighter_train: training mode with hitboxes shown, player 1 on a pad script (the training gate)
cw_add_app(fighter_train SOURCES ${SRC} PACK ${PACK})
target_compile_definitions(fighter_train PRIVATE FIGHTER_SCRIPTED=1 FIGHTER_TRAIN=1)
add_dependencies(fighter_train_pack fighter_test_pack)
