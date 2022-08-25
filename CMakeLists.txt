cmake_minimum_required(VERSION 3.8.2)
project(gst-x86-ai)

add_compile_options(-Wall -Werror)
set(CMAKE_CXX_STANDARD 17)
option(TRITON_INFERENCE "option for Triton inference." OFF)

set(GST_PLUGINS_QTI_OSS_INSTALL "/usr/lib/x86_64-linux-gnu")
set(GST_PLUGINS_QTI_OSS_INSTALL_INCDIR "/usr/include")
set(GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR "${GST_PLUGINS_QTI_OSS_INSTALL}")

set(QTI_AIC_DEV "/opt/qti-aic/dev")
set(QTI_AIC_DEV_INCDIR "${QTI_AIC_DEV}/inc")
set(QTI_AIC_DEV_LIBDIR "${QTI_AIC_DEV}/lib/x86_64")

set(GST_VERSION_REQUIRED "1.16.2")
set(PROTOBUF_VERSION_REQUIRED "3.11.4")

set(GST_PLUGINS_QTI_OSS_VERSION 1.0)
set(GST_PLUGINS_QTI_OSS_PACKAGE ${PROJECT_NAME})
set(GST_PLUGINS_QTI_OSS_SUMMARY "QTI open-source Gstreamer Plug-in")
set(GST_PLUGINS_QTI_OSS_LICENSE BSD)
set(GST_PLUGINS_QTI_OSS_ORIGIN "Unknown package origin")

set(GST_QTI_BASE_PATH "${PROJECT_SOURCE_DIR}/gst-plugin-base")

include_directories(
    ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}
    ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/
    ${QTI_AIC_DEV_INCDIR}
    ${QTI_AIC_DEV_INCDIR}/qaic-api-common
    ${GST_QTI_BASE_PATH}
)

link_directories(
    ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
    ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/
    ${QTI_AIC_DEV_LIBDIR}
)

if (TRITON_INFERENCE)
  add_subdirectory(gst-plugin-batch)
  add_subdirectory(gst-plugin-triton)
else ()
  # add_subdirectory(gst-ml-metadata)
  add_subdirectory(gst-plugin-base)
  add_subdirectory(gst-plugin-mlaic)
  add_subdirectory(gst-plugin-batch)
  add_subdirectory(gst-plugin-metamux)
  add_subdirectory(gst-plugin-mldemux)
  add_subdirectory(gst-plugin-mlvdetection)
  add_subdirectory(gst-plugin-mlvclassification)
  add_subdirectory(gst-plugin-mlvsegmentation)
  add_subdirectory(gst-plugin-mlvconverter)
  add_subdirectory(gst-plugin-triton)
  add_subdirectory(gst-plugin-vsplit)
  add_subdirectory(gst-plugin-videomark)
endif ()

############################################################
# Create Debian package
############################################################
set(CPACK_GENERATOR "DEB")

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_CONTACT "QTI")
set(CPACK_PACKAGE_VERSION "1.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Gstreamer plugins that support AI Inference")
set(CPACK_SYSTEM_NAME "Ubuntu")

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
set(CPACK_DEBIAN_RUNTIME_DEBUGINFO_PACKAGE ON)
include(CPack)
