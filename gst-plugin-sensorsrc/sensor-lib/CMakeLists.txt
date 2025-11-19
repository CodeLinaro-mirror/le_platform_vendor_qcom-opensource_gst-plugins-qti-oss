# Minimum CMake version required
cmake_minimum_required(VERSION 3.10)

# Project name and version
project(GST_Sensors VERSION 1.0 LANGUAGES C CXX)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -fexceptions")

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(SENSORSSEE REQUIRED sensors-see)
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})

# Add include directories from pkg-config
include_directories(${SENSORSSEE_INCLUDE_DIRS} ${GST_INCLUDE_DIRS})

# Add library paths from pkg-config
link_directories(${SENSORSSEE_LIBRARY_DIRS})

# Define sources for the executables
set(SENSOR_WRAPPER_LIB_SOURCES lib-sensor.cc)
set(SENSOR_WRAPPER_SHARED_LIB sensor-wrapper)

# Build sensor-wrapper shared library
add_library(${SENSOR_WRAPPER_SHARED_LIB} SHARED ${SENSOR_WRAPPER_LIB_SOURCES})

target_link_libraries(${SENSOR_WRAPPER_SHARED_LIB} ${SENSORSSEE_LIBRARIES}
    ${GST_LIBRARIES})

# Install the shared library to the appropriate directory
install(TARGETS ${SENSOR_WRAPPER_SHARED_LIB}
    LIBRARY DESTINATION lib
)

# Install the public header files
install(FILES lib-sensor-c-api.h
    DESTINATION include/sensor-wrapper
)
