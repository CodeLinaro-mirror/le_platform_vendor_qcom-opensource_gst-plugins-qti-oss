cmake_minimum_required(VERSION 2.6.4)
project(venc-omx-sample)

set(ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wno-int-conversion")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)

add_definitions(
    -D__AGL__
    -D_GNU_SOURCE
    -D_LINUX_
    -DSUPPORT_CONFIG_INTRA_REFRESH
)
add_definitions (${GLIB_CFLAGS_OTHER})

set(SOURCE_FILES
    ${ROOT_PATH}/video_sample.cpp
    ${ROOT_PATH}/memory_manager.cpp
    ${ROOT_PATH}/omx_utils.cpp
    ${ROOT_PATH}/video_queue.cpp
    ${ROOT_PATH}/config_utils.cpp
    ${ROOT_PATH}/cpu_utils.cpp
    ${ROOT_PATH}/video_types.cpp
)

add_executable (venc-omx-sample ${SOURCE_FILES})

include_directories (${SYSROOTINC_PATH})
include_directories (${SYSROOT_INCLUDEDIR})
include_directories (${GLIB_INCLUDE_DIRS})

link_directories(${SYSROOT_LIBDIR})
link_directories(${GLIB_LIBRARY_DIRS})

target_link_libraries (venc-omx-sample ${GLIB_LIBRARIES})
target_link_libraries (venc-omx-sample pthread)
target_link_libraries (venc-omx-sample OmxCore)
target_link_libraries (venc-omx-sample OmxVenc)
target_link_libraries (venc-omx-sample ion)
target_link_libraries (venc-omx-sample gbm)


install (TARGETS venc-omx-sample DESTINATION ${CMAKE_INSTALL_BINDIR})
