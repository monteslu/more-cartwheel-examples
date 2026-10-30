# hordeshooter_levelview: the descent's levels and creatures for review (a cart in romdev) and the
# native lightmap bakes (LEVEL_BAKE=<dir>); see levelview.c. levelbuild.c is the level builder the
# game links too.
cw_add_app(hordeshooter_levelview
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/levelview.c ${CMAKE_CURRENT_LIST_DIR}/levelbuild.c
  PACK ${CMAKE_CURRENT_LIST_DIR}/../../assets/levelview-roots.txt)
target_include_directories(hordeshooter_levelview PRIVATE ${CMAKE_CURRENT_LIST_DIR})
