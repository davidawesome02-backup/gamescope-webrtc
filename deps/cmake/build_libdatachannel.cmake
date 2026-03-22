cmake_minimum_required(VERSION 3.22)
project(MyDataChannelProject LANGUAGES C CXX)

include(ExternalProject)

# --- Paths ---
set(LIBDATACHANNEL_SRC ${CMAKE_SOURCE_DIR}/deps/libdatachannel)
set(LIBDATACHANNEL_BUILD ${CMAKE_BINARY_DIR}/libdatachannel)
set(LIBDATACHANNEL_INSTALL ${LIBDATACHANNEL_BUILD}/install)

set(LIBDATACHANNEL_LIBS
    ${LIBDATACHANNEL_INSTALL}/lib/libdatachannel.a
    ${LIBDATACHANNEL_INSTALL}/lib/libjuice.a
    ${LIBDATACHANNEL_INSTALL}/lib/libsrtp2.a
    ${LIBDATACHANNEL_INSTALL}/lib/libusrsctp.a
)


# --- ExternalProject to build libdatachannel with all features ---
ExternalProject_Add(libdatachannel_build
    SOURCE_DIR ${LIBDATACHANNEL_SRC}
    BINARY_DIR ${LIBDATACHANNEL_BUILD}
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBDATACHANNEL_INSTALL}  # local staging
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DUSE_OPENSSL=ON
        -DUSE_NICE=OFF        # use built-in ICE backend
        -DNO_EXAMPLES=ON
        -DNO_TESTS=ON
        -DNO_MEDIA=OFF        # include media support
        -DNO_WEBSOCKET=OFF    # include websocket support
        -DUSE_SYSTEM_SRTP=OFF
        -DUSE_SYSTEM_USRSCTP=OFF
        -DUSE_SYSTEM_PLUG=OFF
        -DUSE_SYSTEM_JSON=OFF
        -DUSE_SYSTEM_JUICE=OFF

    BUILD_BYPRODUCTS ${LIBDATACHANNEL_LIBS}
)
set_property(TARGET libdatachannel_build PROPERTY EXCLUDE_FROM_ALL TRUE)

# --- Main INTERFACE library ---
add_library(datachannel INTERFACE)
target_include_directories(datachannel INTERFACE
    ${LIBDATACHANNEL_INSTALL}/include
    ${LIBDATACHANNEL_SRC}/deps/json/include
)


# --- Link everything into INTERFACE target ---
# This ensures all transitive dependencies are included
target_link_libraries(datachannel INTERFACE
    "-Wl,--whole-archive"
    ${LIBDATACHANNEL_LIBS}
    "-Wl,--no-whole-archive"

    ssl
    crypto
    pthread
)

# Ensure the ExternalProject builds before any dependent target
add_dependencies(datachannel libdatachannel_build)