cmake_minimum_required(VERSION 3.22)
project(MyDataChannelProject LANGUAGES C CXX)

include(ExternalProject)

# --- Paths ---
set(LIBDATACHANNEL_SRC ${CMAKE_SOURCE_DIR}/deps/libdatachannel)
set(LIBDATACHANNEL_BUILD ${CMAKE_BINARY_DIR}/libdatachannel)
set(LIBDATACHANNEL_INSTALL ${LIBDATACHANNEL_BUILD}/install)

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
        -DBUILD_EXAMPLES=OFF
        -DBUILD_TESTING=OFF
        -DNO_MEDIA=OFF        # include media support
        -DNO_WEBSOCKET=OFF    # include websocket support
        -DUSE_SYSTEM_SRTP=OFF
        -DUSE_SYSTEM_USRSCTP=OFF
        -DUSE_SYSTEM_PLUG=OFF
        -DUSE_SYSTEM_JSON=OFF
        -DUSE_SYSTEM_JUICE=OFF
)
set_property(TARGET libdatachannel_build PROPERTY EXCLUDE_FROM_ALL TRUE)

# --- Main INTERFACE library ---
add_library(datachannel INTERFACE)
target_include_directories(datachannel INTERFACE
    ${LIBDATACHANNEL_INSTALL}/include
)


# --- JSON (header-only) ---
add_library(datachannel_json INTERFACE)
target_include_directories(datachannel_json INTERFACE
    ${LIBDATACHANNEL_SRC}/deps/json/include
)


file(GLOB LIBDATACHANNEL_STATIC_LIBS "${LIBDATACHANNEL_INSTALL}/lib/*.a")

# --- Link everything into INTERFACE target ---
# This ensures all transitive dependencies are included
target_link_libraries(datachannel INTERFACE
    ${LIBDATACHANNEL_STATIC_LIBS}
    datachannel_json
    ssl
    crypto
    pthread
)

# Ensure the ExternalProject builds before any dependent target
add_dependencies(datachannel libdatachannel_build)