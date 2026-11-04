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
            execute_process(COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/zmusic-build
                                    --target install --parallel
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

set(ZDOOM_EXE_NAME psdoom-${PSDOOM_ENGINE} CACHE FILEPATH "Name of the executable to create" FORCE)

function(psdoom_add_natives)
    target_sources(zdoom PRIVATE ${PSD_ZD}/psdoom_zs.cpp)
    target_compile_definitions(zdoom PRIVATE
        PSDOOM_DATADIR="${PSD_ROOT}/data"
        PSDOOM_SHAREDIR="${CMAKE_INSTALL_PREFIX}/share/psdoom")
    target_link_libraries(zdoom psd)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL psdoom_add_natives)
