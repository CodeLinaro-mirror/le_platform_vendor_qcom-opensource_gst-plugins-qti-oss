cmake_minimum_required(VERSION 2.6.4)
project(vdec-omx-sample)

set(ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wno-int-conversion")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)

add_definitions(
  -D__AGL__
  -D_GNU_SOURCE
  -D_LINUX_
)
add_definitions (${GLIB_CFLAGS_OTHER})

set(SOURCE_FILES
  ${ROOT_PATH}/cpu_utils.cpp
  ${ROOT_PATH}/video_queue.cpp
  ${ROOT_PATH}/video_decode_sample.cpp
  ${ROOT_PATH}/omx_utils.cpp
  ${ROOT_PATH}/video_types.cpp
)

add_executable (vdec-omx-sample ${SOURCE_FILES})

include_directories (${SYSROOTINC_PATH})
include_directories (${SYSROOT_INCLUDEDIR})
include_directories (${GLIB_INCLUDE_DIRS})

link_directories(${SYSROOT_LIBDIR})
link_directories(${GLIB_LIBRARY_DIRS})

target_link_libraries (vdec-omx-sample ${GLIB_LIBRARIES})
target_link_libraries (vdec-omx-sample pthread)
target_link_libraries (vdec-omx-sample OmxCore)
target_link_libraries (vdec-omx-sample OmxVdec)
target_link_libraries (vdec-omx-sample gbm)

install (TARGETS vdec-omx-sample DESTINATION ${CMAKE_INSTALL_BINDIR})

