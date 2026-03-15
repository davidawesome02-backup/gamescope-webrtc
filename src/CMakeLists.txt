cmake_minimum_required(VERSION 3.24)

project(gamescope-webrtc-lib LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

add_library(gamescope-webrtc-lib SHARED
    lib.cpp
    uinput_helper.cpp
    recording.cpp
    webrtc.cpp
)

set_target_properties(gamescope-webrtc-lib PROPERTIES
    OUTPUT_NAME gamescope-webrtc-lib
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

# includes
target_include_directories(gamescope-webrtc-lib PRIVATE
    .
    ${PIPEWIRE_INCLUDE_DIRS}
)

target_compile_options(gamescope-webrtc-lib PRIVATE
    ${PIPEWIRE_CFLAGS_OTHER}
)

# normal runtime dependencies
target_link_libraries(gamescope-webrtc-lib PRIVATE
    ${PIPEWIRE_LIBRARIES}
    Threads::Threads
)

# force static libraries to be fully merged into the .so
target_link_libraries(gamescope-webrtc-lib PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,datachannel>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,ffmpeg>"

    "$<LINK_LIBRARY:WHOLE_ARCHIVE,ffmpeg_avcodec>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,ffmpeg_avformat>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,ffmpeg_avutil>"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,ffmpeg_swscale>"
)

install(TARGETS gamescope-webrtc-lib)