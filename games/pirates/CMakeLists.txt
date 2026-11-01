# Pirates: a shared-screen naval game. The auto cart runs four captains for
# screenshots, smoke tests, and cross-host simulation gates.
set(PIRATES_SRC src/main.c)
configure_file(CREDITS ${CMAKE_CURRENT_SOURCE_DIR}/assets/CREDITS COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/assets/LICENSE COPYONLY)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/pirates.txt)
configure_file(project.cwproj ${CMAKE_CURRENT_SOURCE_DIR}/assets/project.cwproj COPYONLY)
cw_add_app(pirates SOURCES ${PIRATES_SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(pirates_auto SOURCES ${PIRATES_SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(pirates_auto PRIVATE PIRATES_AUTO=1)
add_dependencies(pirates_auto_pack pirates_pack)
target_compile_options(pirates PRIVATE -Wall -Wextra)
target_compile_options(pirates_auto PRIVATE -Wall -Wextra)
