# crispy: Crispy Doom + psDooM, on libpsd
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build the fetched+patched tree:   tools/psdev build crispy
cmake_minimum_required(VERSION 3.16)
project(psdoom-crispy C)

set(UP ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(NOT EXISTS ${UP}/src/doom/d_main.c)
    message(FATAL_ERROR "no upstream tree in ${UP}: run tools/psdev fetch crispy")
endif()

set(PSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../libpsd ${CMAKE_BINARY_DIR}/libpsd)

# keep the dependencies small: no SDL2_mixer/SDL2_net (sound effects still
# play through SDL2; music and netgames need the full build)
set(ENABLE_SDL2_MIXER OFF CACHE BOOL "" FORCE)
set(ENABLE_SDL2_NET OFF CACHE BOOL "" FORCE)
add_subdirectory(${UP} ${CMAKE_BINARY_DIR}/upstream)

target_sources(crispy-doom PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/psdoom/p_psdoom.c
    ${CMAKE_CURRENT_SOURCE_DIR}/psdoom/ps_engine.c)
target_include_directories(crispy-doom PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/psdoom ${UP}/src/doom ${UP}/src)
# the hooks in the game code (library "doom") include p_psdoom.h
target_include_directories(doom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/psdoom)
target_compile_definitions(crispy-doom PRIVATE
    PSDOOM_DATADIR="${CMAKE_CURRENT_SOURCE_DIR}/../../data"
    PSDOOM_SHAREDIR="${CMAKE_INSTALL_PREFIX}/share/psdoom")
target_link_libraries(crispy-doom psd)
set_target_properties(crispy-doom PROPERTIES
    OUTPUT_NAME psdoom-crispy
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
