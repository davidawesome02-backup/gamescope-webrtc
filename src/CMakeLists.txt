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


target_link_libraries(gamescope-webrtc-lib PRIVATE
    "datachannel"

    # I have no idea, this causes an warning, but removing it breaks stuff. I CANNOT figure out how to compile without the warning, I have been here for 2 hours.
    # You know what... lets do a one of those joke posts - Total time spent trying to remove this: 2 hours
    # Total list of things tried:
    # Using linking groups to define all symbols -  [h264_vulkan @ 0x5600597f27b0] [IMGUTILS @ 0x7ffcd1b08fb0] Picture size 480x0 is invalid
    #                                               [h264_vulkan @ 0x5600597f27b0] Ignoring invalid width/height values
    #                                               [h264_vulkan @ 0x5600597f27b0] [IMGUTILS @ 0x7ffcd1b08f90] Picture size 0x0 is invalid
    #                                               [h264_vulkan @ 0x5600597f27b0] Invalid video pixel format: -1
    #                                               terminate called after throwing an instance of 'std::runtime_error'
    #                                               what():  avcodec_open2 failed
    # Using diffrent WHOLE_ARCHIVE for each part -  Multiple deffitions of all the functions in vulkan somehow, works with one, but two fails unless certain ones used and certain order. Three just always fails.
    # 

    # "-Wl,-Map=linker.map"

    "ffmpeg_avutil"
    "ffmpeg_avcodec"
    "ffmpeg_avformat"
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,ffmpeg>"
)


target_link_options(gamescope-webrtc-lib PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
)

target_compile_options(gamescope-webrtc-lib PRIVATE -fvisibility=hidden)





add_executable(gamescope-webrtc example.cpp)

set_target_properties(gamescope-webrtc PROPERTIES
    CXX_STANDARD 17
    OUTPUT_NAME gamescope-webrtc
)

target_include_directories(gamescope-webrtc PRIVATE
    .
)

# normal runtime dependencies
target_link_libraries(gamescope-webrtc PRIVATE
    gamescope-webrtc-lib
)



install(TARGETS gamescope-webrtc-lib)
