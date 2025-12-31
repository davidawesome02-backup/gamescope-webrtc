cmake_minimum_required(VERSION 3.13)
project(gamescope-webrtc)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# Add this line BEFORE any find_package() calls
# list(APPEND CMAKE_PREFIX_PATH "/home/david/local/libdatachannel-install")
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} /home/david/local/libdatachannel-install)

add_subdirectory(src)