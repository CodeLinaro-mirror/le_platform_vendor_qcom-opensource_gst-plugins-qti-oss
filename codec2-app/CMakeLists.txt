#-------------------------------------------------------------------------
# codec2-app - CMake
#-------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.5)

project(codec2-app)

# Compiler flags
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-error=deprecated-declarations -Wno-enum-compare")
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_definitions(-D_AGL_LINUX_)

# codec2-caps Source
set(CODEC2_CAPS_SOURCE "codec2-caps.cc")

add_executable(codec2-caps ${CODEC2_CAPS_SOURCE})
target_link_libraries(codec2-caps dl Threads::Threads)

install(TARGETS "codec2-caps" DESTINATION "${CMAKE_INSTALL_BINDIR}")
