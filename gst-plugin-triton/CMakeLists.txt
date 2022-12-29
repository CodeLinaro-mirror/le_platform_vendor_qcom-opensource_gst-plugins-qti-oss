cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_TRITON
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_BASE
  REQUIRED gstreamer-base-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

find_package(OpenCV REQUIRED)

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories("/usr/local/include/"
                    "/workspace/install/include/"
                    ${CMAKE_CURRENT_BINARY_DIR})
link_directories("/workspace/install/lib/" "/usr/local/lib/")

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# GStreamer Triton plugin.
set(GST_QTI_TRITON gstqtitriton)

add_library(${GST_QTI_TRITON} SHARED
  triton.c
  tritonpads.c
  tritoninfer.cc
  labels/label_mark.cc
)

target_include_directories(${GST_QTI_TRITON} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_TRITON} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${OpenCV_LIBS}
  grpcclient
  httpclient
  protobuf
)

install(
  TARGETS ${GST_QTI_TRITON}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
