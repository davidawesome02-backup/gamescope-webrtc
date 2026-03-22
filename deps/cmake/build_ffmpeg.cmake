include(ExternalProject)

set(FFMPEG_SRC ${CMAKE_SOURCE_DIR}/deps/ffmpeg)
set(FFMPEG_BUILD ${CMAKE_BINARY_DIR}/ffmpeg)
set(FFMPEG_INSTALL ${FFMPEG_BUILD}/install)

set(FFMPEG_LIB_DIR ${FFMPEG_INSTALL}/lib)

set(FFMPEG_LIBS
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libavutil.a
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
        --enable-gpl

        # reduce size
        --disable-programs
        --disable-doc
        --disable-debug
        --disable-autodetect
        --enable-small             # reduces size without breaking helpers
        --disable-everything

        --disable-swresample
        --disable-swscale
        --disable-avdevice
        --disable-avfilter
        --disable-iconv

        # core libs
        --enable-avcodec
        --enable-avformat
        --enable-avutil

        # threading
        --enable-pthreads

        # fast CPU optimizations (if we were using cpu encoding)
        --enable-runtime-cpudetect

        # Makes the binary slightly larger, smallest is using --disable-asm, and dosnt affect preformance much due to vulkan backend use.
        # Due to this, I have it disabled right now, so compiling dosnt require nasm
        # --enable-asm      # 4805328 (at one point)
        --disable-asm       # 4640768


        # H264 support
        --enable-hwaccel=h264_vulkan
        --enable-encoder=h264_vulkan

        --enable-vulkan
        # --enable-vulkan-static

        # containers
        --enable-muxer=rtp

    BUILD_COMMAND make -j
    INSTALL_COMMAND make install


    BUILD_BYPRODUCTS ${FFMPEG_LIBS}

    UPDATE_COMMAND ""
    PATCH_COMMAND ""
)

add_library(ffmpeg INTERFACE)

target_include_directories(ffmpeg INTERFACE
    ${FFMPEG_INSTALL}/include
)

target_link_libraries(ffmpeg INTERFACE
    "-Wl,-Bsymbolic"
    "-Wl,--whole-archive"
    ${FFMPEG_LIBS}
    pthread
    m
    z
    "-Wl,--no-whole-archive"
)