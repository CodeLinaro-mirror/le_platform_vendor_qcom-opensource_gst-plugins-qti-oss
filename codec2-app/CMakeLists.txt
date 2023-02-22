#-------------------------------------------------------------------------
# codec2-app - CMake
#-------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.5)

project(codec2-app)

# Compiler flags
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-error=deprecated-declarations -Wno-enum-compare")
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(ENABLE_INIT_CODEC2_SERVICE ON CACHE BOOL "Enable init_codec2.service : ON|OFF. Default [ON]")
set(DISABLE_BOOT_CAPS OFF CACHE BOOL "Disable init_codec2.service when boot : ON|OFF. Default [OFF]")

find_package(Threads REQUIRED)

add_definitions(-D_AGL_LINUX_)

if(DISABLE_BOOT_CAPS)
    add_definitions(-DDISABLE_BOOT_CAPS)
endif()

# codec2-caps Source
set(CODEC2_CAPS_SOURCE "codec2-caps.cc")

add_executable(codec2-caps ${CODEC2_CAPS_SOURCE})
target_link_libraries(codec2-caps dl Threads::Threads)

install(TARGETS "codec2-caps" DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Install init-codec2 systemd configuration
if(ENABLE_INIT_CODEC2_SERVICE)
    if("${SYSTEMD_UNITDIR}" STREQUAL "")
        set(SYSTEMD_CONFIG_INSTALL_DIR ${CMAKE_INSTALL_SYSCONFDIR}/systemd/system)
    else()
        set(SYSTEMD_CONFIG_INSTALL_DIR ${SYSTEMD_UNITDIR})
    endif()

    install(FILES systemd/init_codec2.service DESTINATION ${SYSTEMD_CONFIG_INSTALL_DIR})
    install(FILES systemd/init_codec2.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/tmpfiles.d)
endif()

