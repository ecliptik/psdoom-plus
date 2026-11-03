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
    src/backend_mock.c
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND PSD_SOURCES src/backend_linux.c)
else()
    list(APPEND PSD_SOURCES src/backend_none.c)
endif()

# psdoom-ng hooks need posix_spawn (everything but DOS)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
    list(APPEND PSD_SOURCES src/backend_cmd.c)
    set(PSD_HAVE_CMD ON)
endif()

add_library(psd STATIC ${PSD_SOURCES})
target_include_directories(psd PUBLIC include PRIVATE src)
set_target_properties(psd PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON
                                     POSITION_INDEPENDENT_CODE ON)
if(NOT PSD_HAVE_CMD)
    target_compile_definitions(psd PRIVATE PSD_NO_CMD=1)
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
    foreach(t core config native cmd)
        add_executable(test_${t} tests/test_${t}.c)
        target_link_libraries(test_${t} PRIVATE psd)
        target_include_directories(test_${t} PRIVATE src tests)
        set_target_properties(test_${t} PROPERTIES C_STANDARD 99)
        add_test(NAME ${t} COMMAND test_${t})
    endforeach()
endif()
