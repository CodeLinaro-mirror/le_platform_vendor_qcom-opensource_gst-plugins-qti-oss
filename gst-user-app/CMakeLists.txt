# GStreamer user application.
set(GST_USER_APP gst-user-app)

add_executable(${GST_USER_APP}
  src/gst_user_app.cpp
)

set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
find_package(PkgConfig REQUIRED)
if ( NOT (PKGCONFIG_FOUND))
      message(FATAL_ERROR "Please Install PPkgConfig: CMake will Exit")
endif()
pkg_check_modules(GST REQUIRED gstreamer-1.0>=1.8)
if ( NOT (GST_FOUND))
      message(FATAL_ERROR "Please Install Gstreamer Dev: CMake will Exit")
endif()
set(ENV{PKG_CONFIG_PATH})
set(USER_APP_INCLUDES inc/)

target_include_directories(${GST_USER_APP} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${USER_APP_INCLUDES}
)

target_link_libraries(${GST_USER_APP} PRIVATE
  ${GST_LIBRARIES}
)

install(
  TARGETS ${GST_USER_APP}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
