# More games on cartwheel-engine. Built the same way as cartwheel-examples
# (scripts/build.sh): against a prebuilt engine SDK, or with the engine from
# source (CARTWHEEL_ENGINE=<checkout> scripts/fetch-deps.sh).
#
# Every games/<name>/CMakeLists.txt is picked up here; a game declares its
# apps with cw_add_app.
if(NOT COMMAND cw_add_app)
  cmake_minimum_required(VERSION 3.20)
  project(more_cartwheel_examples C CXX)
  find_package(cartwheel REQUIRED CONFIG)
endif()

file(GLOB game_lists CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/games/*/CMakeLists.txt)
foreach(list ${game_lists})
  get_filename_component(dir ${list} DIRECTORY)
  add_subdirectory(${dir})
endforeach()
