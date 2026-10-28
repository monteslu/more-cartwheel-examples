# Tux Racer: a work-in-progress reimplementation of Tux Racer 0.61 on cartwheel-engine
# (GPL-2.0-or-later: see LICENSE and CREDITS; the original courses, penguin, music and sounds
# are the GPL data, converted by tools/convert.mjs).
#   tuxracer   the game (practice on 13 courses, the Canadian Cup)
set(SRC src/main.c src/course.c src/course_objects.c src/tux.c src/physics.c src/view.c src/hud.c src/fx.c src/audio.c
        src/textures.c src/tree_mesh.c src/item_mesh.c src/tux_skin.c src/skyring.c)
# the input map and the render size come from project.cwproj, packed with the assets
configure_file(project.cwproj ${CMAKE_CURRENT_SOURCE_DIR}/assets/project.cwproj COPYONLY)
cw_add_app(tuxracer SOURCES ${SRC} PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/tuxracer.txt PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_options(tuxracer PRIVATE -Wall -Wextra)
