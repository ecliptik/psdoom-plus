# og: id Software's linuxdoom-1.10 + SDL3 + psDooM, on libpsd
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build the fetched+patched tree:   tools/psdev build og
cmake_minimum_required(VERSION 3.16)
project(psdoom-og VERSION 0.1.0 LANGUAGES C)

set(OG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/linuxdoom-1.10)
if(NOT EXISTS ${OG_SRC}/d_main.c)
    message(FATAL_ERROR "no upstream tree in ${OG_SRC}: run tools/psdev fetch og")
endif()

set(PSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../libpsd ${CMAKE_BINARY_DIR}/libpsd)

find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
endif()

file(GLOB OG_C ${OG_SRC}/*.c)
list(REMOVE_ITEM OG_C ${OG_SRC}/i_video.c)      # X11: replaced by platform/i_video_sdl.c

add_executable(psdoom-og ${OG_C}
    platform/i_video_sdl.c
    platform/i_audio_sdl.c
    psdoom/p_psdoom.c)
target_include_directories(psdoom-og PRIVATE ${OG_SRC} platform psdoom)
target_link_libraries(psdoom-og PRIVATE psd SDL3::SDL3 m)

# Vanilla Doom relies on 32-bit wrapping arithmetic and loose aliasing.
target_compile_definitions(psdoom-og PRIVATE NORMALUNIX PSDOOM_PLUS
    PSDOOM_DATADIR="${CMAKE_CURRENT_SOURCE_DIR}/../../data"
    PSDOOM_SHAREDIR="${CMAKE_INSTALL_PREFIX}/share/psdoom")
target_compile_options(psdoom-og PRIVATE -std=gnu99 -fno-strict-aliasing -fwrapv
    -Wno-unused -Wno-pointer-sign -Wno-parentheses -Wno-missing-braces)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(psdoom-og PRIVATE -Wno-unused-but-set-variable)
endif()

install(TARGETS psdoom-og RUNTIME DESTINATION bin)
install(FILES ../../data/psdoom1.wad ../../data/psdoom2.wad DESTINATION share/psdoom)
