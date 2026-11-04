# psdoom.cmake - psdoom-plus additions to a DSDA-Doom build
#
# Injected right after DSDA-Doom's own top-level project() with
#   -DCMAKE_PROJECT_dsda-doom_INCLUDE=engines/dsda/psdoom.cmake
# so DSDA-Doom runs as the top-level project, as it expects (tools/psdev
# build does this).
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(PSD_DSDA ${CMAKE_CURRENT_LIST_DIR})
set(PSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

set(PSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${PSD_ROOT}/libpsd ${CMAKE_BINARY_DIR}/libpsd)

# libmad (MP3 music) is optional, but DSDA-Doom takes the library without
# its header (Haiku has one without the other): skip it then
find_path(PSD_MAD_INC mad.h)
if(NOT PSD_MAD_INC)
    set(WITH_MAD OFF CACHE BOOL "" FORCE)
endif()

# Haiku's packaged SDL2_mixer config looks for the shared library in the
# wrong directory and defines no target: define it before DSDA-Doom asks
if(CMAKE_SYSTEM_NAME STREQUAL "Haiku" AND NOT TARGET SDL2_mixer::SDL2_mixer)
    find_library(PSD_SDL2_MIXER SDL2_mixer)
    find_path(PSD_SDL2_MIXER_INC SDL_mixer.h PATH_SUFFIXES SDL2)
    if(PSD_SDL2_MIXER AND PSD_SDL2_MIXER_INC)
        add_library(SDL2_mixer::SDL2_mixer SHARED IMPORTED GLOBAL)
        set_target_properties(SDL2_mixer::SDL2_mixer PROPERTIES
            IMPORTED_LOCATION ${PSD_SDL2_MIXER}
            INTERFACE_INCLUDE_DIRECTORIES ${PSD_SDL2_MIXER_INC})
    endif()
endif()

function(psdoom_add_glue)
    target_sources(dsda-doom PRIVATE
        ${PSD_ROOT}/engines/common/psdoom/p_psdoom.c
        ${PSD_DSDA}/psdoom/ps_engine.c)
    # the hooks include p_psdoom.h; the glue includes ps_defs.h (no p_local.h here)
    target_include_directories(dsda-doom PRIVATE
        ${PSD_ROOT}/engines/common/psdoom ${PSD_DSDA}/psdoom ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(dsda-doom PRIVATE
        PSE_DEFS="ps_defs.h" PSE_DBOOLEAN
        PSDOOM_DATADIR="${PSD_ROOT}/data"
        PSDOOM_SHAREDIR="${CMAKE_INSTALL_PREFIX}/share/psdoom")
    target_link_libraries(dsda-doom PRIVATE psd)
    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        target_link_libraries(dsda-doom PRIVATE network)	# sockets
    endif()
    set_target_properties(dsda-doom PROPERTIES OUTPUT_NAME psdoom-dsda)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL psdoom_add_glue)
