# psdoom.cmake - psdoom-plus additions to a Woof! build
#
# Injected right after Woof!'s own top-level project() with
#   -DCMAKE_PROJECT_Woof_INCLUDE=engines/woof/psdoom.cmake
# so Woof! runs as the top-level project, as it expects (tools/psdev build
# does this).
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(PSD_WOOF ${CMAKE_CURRENT_LIST_DIR})
set(PSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

set(PSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${PSD_ROOT}/libpsd ${CMAKE_BINARY_DIR}/libpsd)

set(WITH_DISCORD_RPC OFF CACHE BOOL "" FORCE)

# Woof! prefers CMake package configs; some (Haiku's OpenAL) still ask for
# a CMake older than 3.5, which CMake 4 refuses without this
if(NOT CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# Woof! 16 needs SDL3 3.4, newer than some current distributions ship
# (Debian 13 has 3.2): build a static SDL3 at configure time if so
find_package(SDL3 3.4.0 QUIET CONFIG)
if(NOT SDL3_FOUND)
    set(SDL_TAG release-3.4.20)
    set(SDL_SRC ${CMAKE_BINARY_DIR}/sdl3-src)
    set(SDL_OUT ${CMAKE_BINARY_DIR}/sdl3)
    if(NOT EXISTS ${SDL_OUT}/lib/cmake/SDL3/SDL3Config.cmake)
        message(STATUS "System SDL3 is older than 3.4: building ${SDL_TAG} into ${SDL_OUT}")
        if(NOT EXISTS ${SDL_SRC}/CMakeLists.txt)
            execute_process(COMMAND git clone -q --depth 1 --branch ${SDL_TAG}
                                    https://github.com/libsdl-org/SDL.git ${SDL_SRC}
                            RESULT_VARIABLE rc)
            if(rc)
                message(FATAL_ERROR "could not fetch SDL ${SDL_TAG}")
            endif()
        endif()
        include(ProcessorCount)
        ProcessorCount(sdl_jobs)
        if(sdl_jobs EQUAL 0)
            set(sdl_jobs 2)
        endif()
        execute_process(COMMAND ${CMAKE_COMMAND} -S ${SDL_SRC} -B ${CMAKE_BINARY_DIR}/sdl3-build
                                -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${SDL_OUT}
                                -DCMAKE_INSTALL_LIBDIR=lib -DSDL_SHARED=OFF -DSDL_STATIC=ON
                                -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
                                -DSDL_X11_XTEST=OFF
                                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                        RESULT_VARIABLE rc OUTPUT_QUIET)
        if(NOT rc)
            execute_process(COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/sdl3-build
                                    --target install --parallel ${sdl_jobs}
                            RESULT_VARIABLE rc OUTPUT_QUIET)
        endif()
        if(rc)
            message(FATAL_ERROR "could not build SDL ${SDL_TAG}")
        endif()
    endif()
    set(SDL3_DIR ${SDL_OUT}/lib/cmake/SDL3 CACHE PATH "" FORCE)
endif()

function(psdoom_add_glue)
    target_sources(woof PRIVATE
        ${PSD_ROOT}/engines/common/psdoom/p_psdoom.c
        ${PSD_WOOF}/psdoom/ps_engine.c)
    # the hooks include p_psdoom.h; the glue includes ps_defs.h (no p_local.h here)
    target_include_directories(woof PRIVATE
        ${PSD_ROOT}/engines/common/psdoom ${PSD_WOOF}/psdoom ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(woof PRIVATE
        PSE_DEFS="ps_defs.h"
        PSDOOM_DATADIR="${PSD_ROOT}/data"
        PSDOOM_SHAREDIR="${CMAKE_INSTALL_PREFIX}/share/psdoom")
    target_link_libraries(woof PRIVATE psd)
    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        target_link_libraries(woof PRIVATE network)	# sockets
    endif()
    set_target_properties(woof PROPERTIES
        OUTPUT_NAME psdoom-woof
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    # Woof! looks for woof.pk3 beside the executable
    add_custom_target(psdoom-woof-pk3
        COMMAND ${CMAKE_COMMAND} -E copy "${BASE_PK3_PATH}" "${CMAKE_BINARY_DIR}"
        DEPENDS "${BASE_PK3_PATH}")
    add_dependencies(psdoom-woof-pk3 woof-base)
    add_dependencies(woof psdoom-woof-pk3)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL psdoom_add_glue)
