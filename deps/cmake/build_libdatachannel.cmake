include(ExternalProject)

set(LIBDATACHANNEL_SRC ${CMAKE_SOURCE_DIR}/deps/libdatachannel)
set(LIBDATACHANNEL_BUILD ${CMAKE_BINARY_DIR}/libdatachannel)
set(LIBDATACHANNEL_INSTALL ${LIBDATACHANNEL_BUILD}/install)

set(LIBDATACHANNEL_LIB
    ${LIBDATACHANNEL_INSTALL}/lib/libdatachannel.a
)

ExternalProject_Add(libdatachannel_build
    SOURCE_DIR ${LIBDATACHANNEL_SRC}
    BINARY_DIR ${LIBDATACHANNEL_BUILD}

    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LIBDATACHANNEL_INSTALL}
        -DCMAKE_BUILD_TYPE=Release

        # static
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON

        # TLS backend
        -DUSE_OPENSSL=ON
        -DUSE_GNUTLS=OFF
        -DUSE_MBEDTLS=OFF

        # use built-in ICE backend
        -DUSE_NICE=OFF

        # reduce size
        -DBUILD_EXAMPLES=OFF
        -DBUILD_TESTING=OFF

    BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel
    INSTALL_COMMAND ${CMAKE_COMMAND} --install .

    BUILD_BYPRODUCTS ${LIBDATACHANNEL_LIB}

    UPDATE_COMMAND ""
    PATCH_COMMAND ""
)

# --- libdatachannel static import ---

add_library(libdatachannel STATIC IMPORTED GLOBAL)

set_target_properties(libdatachannel PROPERTIES
    IMPORTED_LOCATION ${LIBDATACHANNEL_INSTALL}/lib/libdatachannel.a
)

add_dependencies(libdatachannel libdatachannel_build)

# --- JSON (header-only) ---

add_library(datachannel_json INTERFACE)

target_include_directories(datachannel_json INTERFACE
    ${LIBDATACHANNEL_SRC}/deps/json/include
)

# --- main interface target ---

add_library(datachannel INTERFACE)

target_include_directories(datachannel INTERFACE
    ${LIBDATACHANNEL_INSTALL}/include
)

target_link_libraries(datachannel INTERFACE
    libdatachannel
    datachannel_json
    ssl
    crypto
    pthread
)