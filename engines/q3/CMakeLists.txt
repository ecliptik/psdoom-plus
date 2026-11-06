# q3: OpenArena's game code (qagame) + psDooM, on libpsd
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build the fetched+patched tree:   tools/psdev build q3
#
# No engine patch: the stock ioquake3 engine loads this native qagame from
# the "psdoom" mod directory (vm_game 0), with OpenArena 0.8.8's data.
# psdoom-q3 (from psdoom-q3.sh) starts it.
cmake_minimum_required(VERSION 3.16)
project(psdoom-q3 VERSION 0.2.0 LANGUAGES C)

set(Q3_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/code)
if(NOT EXISTS ${Q3_SRC}/game/g_main.c)
    message(FATAL_ERROR "no upstream tree in ${Q3_SRC}: run tools/psdev fetch q3")
endif()

set(PSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../libpsd ${CMAKE_BINARY_DIR}/libpsd)

# ioquake3's ARCH_STRING, which the engine appends to the module name
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" Q3_PROC)
if(Q3_PROC MATCHES "^(x86_64|amd64)$")
    set(Q3_ARCH x86_64)
elseif(Q3_PROC MATCHES "^i.86$")
    set(Q3_ARCH x86)
elseif(Q3_PROC MATCHES "^(aarch64|arm64)$")
    set(Q3_ARCH arm64)
else()
    set(Q3_ARCH ${Q3_PROC})
endif()

# the Makefile's Q3GOBJ (baseq3 qagame)
set(Q3_GAME g_main ai_chat ai_cmd ai_dmnet ai_dmq3 ai_main ai_team ai_vcmd
    bg_misc bg_lib bg_pmove bg_slidemove g_active g_arenas g_admin g_bot g_client
    g_cmds g_cmds_ext g_combat g_items bg_alloc g_fileops g_killspree g_misc
    g_missile g_mover g_playerstore g_session g_spawn g_svcmds g_svcmds_ext
    g_target g_team g_trigger g_unlagged g_utils g_vote g_weapon g_syscalls)
list(TRANSFORM Q3_GAME PREPEND ${Q3_SRC}/game/)
list(TRANSFORM Q3_GAME APPEND .c)

add_library(qagame MODULE ${Q3_GAME}
    ${Q3_SRC}/qcommon/q_math.c
    ${Q3_SRC}/qcommon/q_shared.c
    psdoom/g_psd.c)
target_include_directories(qagame PRIVATE ${Q3_SRC}/game psdoom)
target_compile_definitions(qagame PRIVATE QAGAME)
target_link_libraries(qagame PRIVATE psd m)
set_target_properties(qagame PROPERTIES
    PREFIX "" OUTPUT_NAME qagame${Q3_ARCH}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/psdoom
    C_STANDARD 99 C_EXTENSIONS ON)
# 2014 code: tentative definitions in headers, and warnings that newer
# compilers make errors
target_compile_options(qagame PRIVATE -fno-strict-aliasing -fcommon
    -Wno-unused -Wno-pointer-sign -Wno-parentheses -Wno-missing-braces
    -Wno-format-truncation -Wno-format-overflow -Wno-stringop-truncation
    -Wno-unknown-warning-option)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(qagame PRIVATE -Wno-unused-but-set-variable
        -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types
        -Wno-error=int-conversion)
endif()

# the launcher, next to the mod directory; only once the module has linked,
# so a missing build/q3/psdoom-q3 means a failed build
add_custom_target(psdoom-q3 ALL
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/psdoom-q3.sh
            ${CMAKE_BINARY_DIR}/psdoom-q3
    COMMAND chmod 755 ${CMAKE_BINARY_DIR}/psdoom-q3
    DEPENDS qagame psdoom-q3.sh)

install(TARGETS qagame LIBRARY DESTINATION lib/psdoom-plus/q3/psdoom)
install(PROGRAMS psdoom-q3.sh DESTINATION bin RENAME psdoom-q3)
