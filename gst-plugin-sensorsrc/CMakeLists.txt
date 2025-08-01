cmake_minimum_required(VERSION 3.10)

project(GST_PLUGIN_QTI_OSS_SENSOR_SRC
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -fexceptions")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig REQUIRED)

pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# GStreamer source plugin.
set(GST_QTI_SENSORSRC qtisensorsrc)

add_library(${GST_QTI_SENSORSRC} SHARED
  qtisensorsrc.c
)

SET(POSITION_INDEPENDENT_CODE ON)

target_include_directories(${GST_QTI_SENSORSRC} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_SENSORSRC} PRIVATE
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_SENSORSRC}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

add_subdirectory(sensor-lib)
