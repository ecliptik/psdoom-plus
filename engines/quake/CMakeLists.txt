# quake: QuakeSpasm + psDooM, on libpsd
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build the fetched+patched tree:   tools/psdev build quake
#
# The engine is QuakeSpasm (SDL2 + OpenGL); the game is any Quake's own
# progs.dat (id's, shareware or registered): no QuakeC changes.
# psdoom-quake (from psdoom-quake.sh) finds the data and starts it.
cmake_minimum_required(VERSION 3.16)
project(psdoom-quake VERSION 0.1.1 LANGUAGES C)

set(QS ${CMAKE_CURRENT_SOURCE_DIR}/src/Quake)
if(NOT EXISTS ${QS}/host.c)
    message(FATAL_ERROR "no upstream tree in ${QS}: run tools/psdev fetch quake")
endif()

set(PSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../libpsd ${CMAKE_BINARY_DIR}/libpsd)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(OpenGL REQUIRED)

# QuakeSpasm's Makefile (OBJS) with SDL2 and the WAV codec only
set(QS_C strlcat strlcpy
    gl_refrag gl_rlight gl_rmain gl_fog gl_rmisc r_part r_world gl_screen gl_sky gl_warp
    gl_vidsdl gl_draw image gl_texmgr gl_mesh r_sprite r_alias r_brush gl_model
    in_sdl snd_dma snd_mix snd_mem bgmusic snd_codec snd_flac snd_wave snd_vorbis snd_opus
    snd_mp3 snd_mp3tag snd_mikmod snd_modplug snd_xmp snd_umx snd_sdl cd_sdl
    net_bsd net_udp net_dgrm net_loop net_main
    chase cl_demo cl_input cl_main cl_parse cl_tent console keys menu sbar view wad
    cmd common miniz crc cvar cfgfile host host_cmd mathlib pr_cmds pr_edict pr_exec
    sv_main sv_move sv_phys sv_user world zone pl_linux sys_sdl_unix main_sdl)
list(TRANSFORM QS_C PREPEND ${QS}/)
list(TRANSFORM QS_C APPEND .c)

add_executable(quakespasm-psdoom ${QS_C} psdoom/sv_psd.c)
target_include_directories(quakespasm-psdoom PRIVATE ${QS} psdoom)
target_compile_definitions(quakespasm-psdoom PRIVATE USE_SDL2 USE_CODEC_WAVE NDEBUG)
target_compile_options(quakespasm-psdoom PRIVATE -Wno-trigraphs)
target_link_libraries(quakespasm-psdoom PRIVATE psd PkgConfig::SDL2 OpenGL::GL m)
if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    target_link_libraries(quakespasm-psdoom PRIVATE network)
endif()

# the launcher, only once the engine has linked (a missing
# build/quake/psdoom-quake means a failed build)
add_custom_target(psdoom-quake ALL
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/psdoom-quake.sh
            ${CMAKE_BINARY_DIR}/psdoom-quake
    COMMAND chmod 755 ${CMAKE_BINARY_DIR}/psdoom-quake
    DEPENDS quakespasm-psdoom psdoom-quake.sh)

install(TARGETS quakespasm-psdoom RUNTIME DESTINATION lib/psdoom-plus/quake)
install(PROGRAMS psdoom-quake.sh DESTINATION bin RENAME psdoom-quake)
