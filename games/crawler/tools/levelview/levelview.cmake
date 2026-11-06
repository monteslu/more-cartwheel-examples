# crawler_levelview: a floor as the game builds it, for review (a cart in romdev) and for the native bakes of every
# floor's lightmap (--bake <dir>); see levelview.c. mazebuild.c is the builder the game links too.
cw_add_app(crawler_levelview
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/levelview.c ${CMAKE_CURRENT_LIST_DIR}/../../mazebuild.c ${CMAKE_CURRENT_LIST_DIR}/../../maze.c ${CMAKE_CURRENT_LIST_DIR}/../../sim.c
  PACK ${CMAKE_CURRENT_LIST_DIR}/../../assets/roots.txt
  PROGRAMS ${CMAKE_CURRENT_LIST_DIR}/programs/crawler_levelview.txt)
target_include_directories(crawler_levelview PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../..)
add_dependencies(crawler_levelview_pack crawler_pack)
