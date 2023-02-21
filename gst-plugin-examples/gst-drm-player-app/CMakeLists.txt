# GStreamer DRM Player app
set(GST_EXAMPLE_BIN gst-drm-player-app)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

find_package(LibXml2 REQUIRED)
find_package(CURL REQUIRED)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${SYSROOT_INCDIR}
  ${SYSROOT_INCDIR}/media/drm
  ${CURL_INCLUDE_DIR}
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  dl
  utils
  cutils
  ${GST_LIBRARIES}
  ${CURL_LIBRARIES}
  LibXml2::LibXml2
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
