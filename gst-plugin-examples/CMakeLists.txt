cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_EXAMPLES LANGUAGES C CXX)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
set(GST_VERSION_REQUIRED 1.18.5)
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

add_subdirectory(gst-drm-player-example)
