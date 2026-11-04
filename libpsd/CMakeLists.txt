# libpsd - processes as monsters
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.13)
project(libpsd VERSION 0.1.0 LANGUAGES C)

option(PSD_BUILD_TESTS "Build libpsd unit tests" ON)
option(PSD_WERROR "Treat warnings as errors" OFF)

set(PSD_SOURCES
    src/psd_core.c
    src/psd_config.c
    src/psd_platform.c
    src/psd_util.c
    src/psd_place.c
    src/backend_mock.c
)

# native process table
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND PSD_SOURCES src/backend_linux.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    list(APPEND PSD_SOURCES src/backend_freebsd.c src/backend_posix.c)
elseif(CMAKE_SYSTEM_NAME MATCHES "^(OpenBSD|NetBSD)$")
    list(APPEND PSD_SOURCES src/backend_openbsd.c src/backend_posix.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    list(APPEND PSD_SOURCES src/backend_haiku.c src/backend_posix.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "DOS")
    list(APPEND PSD_SOURCES src/backend_dos.c)     # the MCB chain (DJGPP)
else()
    list(APPEND PSD_SOURCES src/backend_none.c)
endif()

# psdoom-ng hooks need posix_spawn (everything but DOS)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
    list(APPEND PSD_SOURCES src/backend_cmd.c)
    set(PSD_HAVE_CMD ON)
endif()

# net backend (psdoomd client): BSD sockets; on DOS our own UDP (dos_net.c)
list(APPEND PSD_SOURCES src/backend_net.c)
if(CMAKE_SYSTEM_NAME STREQUAL "DOS")
    list(APPEND PSD_SOURCES src/dos_net.c)
endif()

add_library(psd STATIC ${PSD_SOURCES})
target_include_directories(psd PUBLIC include PRIVATE src)
set_target_properties(psd PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON
                                     POSITION_INDEPENDENT_CODE ON)
if(NOT PSD_HAVE_CMD)
    target_compile_definitions(psd PRIVATE PSD_NO_CMD=1)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "DOS")
    target_compile_definitions(psd PRIVATE PSD_DOSNET)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    target_link_libraries(psd PUBLIC network)
endif()

# psdoomd: serve this machine's processes to a remote psdoom-plus (-psnet)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
    add_executable(psdoomd psdoomd.c)
    target_link_libraries(psdoomd PRIVATE psd)
    set_target_properties(psdoomd PROPERTIES C_STANDARD 99)
    install(TARGETS psdoomd RUNTIME DESTINATION bin)
endif()
find_library(PSD_LIBM m)
if(PSD_LIBM)
    target_link_libraries(psd PUBLIC ${PSD_LIBM})
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(psd PRIVATE -Wall -Wextra -Wshadow -Wstrict-prototypes)
    if(PSD_WERROR)
        target_compile_options(psd PRIVATE -Werror)
    endif()
endif()

if(PSD_BUILD_TESTS)
    enable_testing()
    foreach(t core config native cmd place)
        add_executable(test_${t} tests/test_${t}.c)
        target_link_libraries(test_${t} PRIVATE psd)
        target_include_directories(test_${t} PRIVATE src tests)
        set_target_properties(test_${t} PROPERTIES C_STANDARD 99)
        add_test(NAME ${t} COMMAND test_${t})
    endforeach()
endif()
