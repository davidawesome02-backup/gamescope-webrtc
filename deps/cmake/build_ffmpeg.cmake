include(ExternalProject)

set(FFMPEG_SRC ${CMAKE_SOURCE_DIR}/deps/ffmpeg)
set(FFMPEG_BUILD ${CMAKE_BINARY_DIR}/ffmpeg)
set(FFMPEG_INSTALL ${FFMPEG_BUILD}/install)

set(FFMPEG_LIB_DIR ${FFMPEG_INSTALL}/lib)

set(FFMPEG_LIBS
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    ${FFMPEG_LIB_DIR}/libswscale.a
)

ExternalProject_Add(ffmpeg_build
    SOURCE_DIR ${FFMPEG_SRC}
    BINARY_DIR ${FFMPEG_BUILD}

    CONFIGURE_COMMAND
        ${FFMPEG_SRC}/configure
        --prefix=${FFMPEG_INSTALL}

        # static build
        --disable-shared
        --enable-static
        --enable-pic
        --enable-lto

        # reduce size
        --disable-programs
        --disable-doc
        --disable-debug
        --disable-autodetect

        # remove everything
        --disable-everything

        # core libs
        --enable-avcodec
        --enable-avformat
        --enable-avutil
        --enable-swscale

        # threading
        --enable-pthreads

        # fast CPU optimizations (cant use asm because of PIC)
        --enable-runtime-cpudetect

        # unfortunately required to make ffmpeg compile into a single .a file :(
        --disable-asm

        # H264 support
        # --enable-decoder=h264
        # --enable-parser=h264
        # --enable-encoder=libx264
        # --enable-encoder=libx264rgb
        --enable-encoder=h264
        --enable-encoder=h264_vaapi

        --enable-hwaccel=h264_vulkan
        --enable-encoder=h264_vulkan

        # --enable-libx264
        --enable-vulkan # not real?
        # --enable-vulkan-static
        # --enable-vaapi
        --enable-gpl


        # containers
        --enable-muxer=flv
        --enable-muxer=rtp
        --enable-muxer=rtp_mpegts
        # --enable-demuxer=flv

        # streaming
        --enable-network
        --enable-protocol=rtmp

        # scaling
        --enable-swscale

        --enable-parser=mpegvideo  # parser framework dependency
        --enable-avutil            # ensure libavutil builds
        --enable-small             # reduces size without breaking helpers

    BUILD_COMMAND make -j
    INSTALL_COMMAND make install


    BUILD_BYPRODUCTS ${FFMPEG_LIBS}

    UPDATE_COMMAND ""
    PATCH_COMMAND ""
)

# imported static libs

add_library(ffmpeg_avcodec STATIC IMPORTED GLOBAL)
add_library(ffmpeg_avformat STATIC IMPORTED GLOBAL)
add_library(ffmpeg_avutil STATIC IMPORTED GLOBAL)
add_library(ffmpeg_swscale STATIC IMPORTED GLOBAL)

set_target_properties(ffmpeg_avcodec PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_INSTALL}/lib/libavcodec.a)

set_target_properties(ffmpeg_avformat PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_INSTALL}/lib/libavformat.a)

set_target_properties(ffmpeg_avutil PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_INSTALL}/lib/libavutil.a)

set_target_properties(ffmpeg_swscale PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_INSTALL}/lib/libswscale.a)

add_dependencies(ffmpeg_avcodec ffmpeg_build)
add_dependencies(ffmpeg_avformat ffmpeg_build)
add_dependencies(ffmpeg_avutil ffmpeg_build)
add_dependencies(ffmpeg_swscale ffmpeg_build)

# interface target for easy linking

add_library(ffmpeg INTERFACE)

target_include_directories(ffmpeg INTERFACE
    ${FFMPEG_INSTALL}/include
)

target_link_libraries(ffmpeg INTERFACE
    ffmpeg_avcodec
    ffmpeg_avformat
    ffmpeg_avutil
    ffmpeg_swscale
    pthread
    m
    z
)