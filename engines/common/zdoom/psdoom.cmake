# psdoom.cmake - psdoom-plus additions to a GZDoom/UZDoom build
#
# Injected right after the engine's own top-level project() with
#   -DCMAKE_PROJECT_<Name>_INCLUDE=engines/common/zdoom/psdoom.cmake
#   -DPSDOOM_ENGINE=<gzdoom|uzdoom>
# so the engine's build runs as the top-level project, as it expects.
# (tools/psdev build does this.)
#
# SPDX-License-Identifier: GPL-2.0-or-later (the combined program is GPL-3.0)

set(PSD_ZD ${CMAKE_CURRENT_LIST_DIR})
set(PSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

set(PSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${PSD_ROOT}/libpsd ${CMAKE_BINARY_DIR}/libpsd)

# ZScript natives may only be declared in the engine's own resource: the
# patch includes zscript/psdoom.zs, which comes from our tree
configure_file(${PSD_ZD}/psdoom.zs ${CMAKE_SOURCE_DIR}/wadsrc/static/zscript/psdoom.zs COPYONLY)

# GZDoom needs ZMusic installed (UZDoom bundles it). Build it as a separate
# project at configure time: it can't share a project with GZDoom (both
# define a "miniz" target).
if(PSDOOM_ENGINE STREQUAL "gzdoom" AND NOT ZMUSIC_LIBRARIES)
    set(ZM_SRC ${CMAKE_BINARY_DIR}/zmusic-src)
    set(ZM_OUT ${CMAKE_BINARY_DIR}/zmusic)
    if(NOT EXISTS ${ZM_SRC}/CMakeLists.txt)
        execute_process(COMMAND git clone -q --depth 1 --branch 1.3.0
                                https://github.com/ZDoom/ZMusic.git ${ZM_SRC}
                        RESULT_VARIABLE rc)
        if(rc)
            message(FATAL_ERROR "could not fetch ZMusic 1.3.0")
        endif()
    endif()
    if(NOT EXISTS ${ZM_OUT}/include/zmusic.h)
        message(STATUS "Building ZMusic 1.3.0 into ${ZM_OUT}")
        execute_process(COMMAND ${CMAKE_COMMAND} -S ${ZM_SRC} -B ${CMAKE_BINARY_DIR}/zmusic-build
                                -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${ZM_OUT}
                                -DCMAKE_INSTALL_LIBDIR=lib
                        RESULT_VARIABLE rc OUTPUT_QUIET)
        if(NOT rc)
            # a bare --parallel lets make run unlimited jobs, which runs a 4 GB
        # guest out of memory: one job per CPU, and no more than one per 1.5 GB
        include(ProcessorCount)
        ProcessorCount(zm_jobs)
        cmake_host_system_information(RESULT zm_mem QUERY TOTAL_PHYSICAL_MEMORY)
        math(EXPR zm_cap "${zm_mem} / 1536")
        if(zm_jobs EQUAL 0 OR zm_jobs GREATER zm_cap)
            set(zm_jobs ${zm_cap})
        endif()
        if(zm_jobs LESS 1)
            set(zm_jobs 1)
        endif()
        execute_process(COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/zmusic-build
                                    --target install --parallel ${zm_jobs}
                            RESULT_VARIABLE rc OUTPUT_QUIET)
        endif()
        if(rc)
            message(FATAL_ERROR "could not build ZMusic")
        endif()
    endif()
    find_library(PSD_ZMUSIC_LIB zmusic PATHS ${ZM_OUT}/lib NO_DEFAULT_PATH REQUIRED)
    set(ZMUSIC_INCLUDE_DIR ${ZM_OUT}/include CACHE PATH "" FORCE)
    set(ZMUSIC_LIBRARIES ${PSD_ZMUSIC_LIB} CACHE STRING "" FORCE)
endif()
# GZDoom links with its install RPATH ($ORIGIN): keep libzmusic beside it
if(PSDOOM_ENGINE STREQUAL "gzdoom" AND EXISTS ${CMAKE_BINARY_DIR}/zmusic/lib)
    file(COPY ${CMAKE_BINARY_DIR}/zmusic/lib/ DESTINATION ${CMAKE_BINARY_DIR}
         FILES_MATCHING PATTERN "libzmusic*")
endif()

# ZVulkan includes <X11/Xlib.h> without asking for X11's include path, which
# is only on the default path on Linux (BSDs keep it under /usr/local or /usr/X11R6)
find_package(X11 QUIET)
if(X11_FOUND)
    include_directories(SYSTEM ${X11_INCLUDE_DIR})
else()
    set(VULKAN_USE_XLIB OFF CACHE BOOL "" FORCE)
endif()
# Haiku has no Vulkan surface support in SDL: build the OpenGL renderer only
if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    set(HAVE_VULKAN OFF CACHE BOOL "" FORCE)
endif()

set(ZDOOM_EXE_NAME psdoom-${PSDOOM_ENGINE} CACHE FILEPATH "Name of the executable to create" FORCE)

function(psdoom_add_natives)
    target_sources(zdoom PRIVATE ${PSD_ZD}/psdoom_zs.cpp)
    target_compile_definitions(zdoom PRIVATE
        PSDOOM_DATADIR="${PSD_ROOT}/data"
        PSDOOM_SHAREDIR="${CMAKE_INSTALL_PREFIX}/share/psdoom")
    target_link_libraries(zdoom psd)
    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        # sockets live in libnetwork; zipdir's fts(3) in libbsd
        target_link_libraries(zdoom network)
        if(TARGET zipdir)
            target_link_libraries(zipdir bsd)
        endif()
    endif()
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL psdoom_add_natives)
