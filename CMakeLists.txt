cmake_minimum_required(VERSION 3.20)

project(minimal_ffmpeg LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# aggressive optimization for speed + size
add_compile_options(
    -O3
    -ffunction-sections
    -fdata-sections
)

add_link_options(
    -Wl,--gc-sections
)

# LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)

if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# add_subdirectory(deps/cmake/build_ffmpeg.cmake)
include(deps/cmake/build_ffmpeg.cmake)
include(deps/cmake/build_libdatachannel.cmake)
add_subdirectory(src)



# cmake_minimum_required(VERSION 3.13)
# project(gamescope-webrtc)

# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# # Add this line BEFORE any find_package() calls
# # list(APPEND CMAKE_PREFIX_PATH "/home/david/local/libdatachannel-install")
# set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} /home/david/local/libdatachannel-install)

# add_subdirectory(src)