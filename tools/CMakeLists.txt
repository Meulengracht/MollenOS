cmake_minimum_required (VERSION 3.13.4)
project (vali-tools)
enable_language (C)

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include (ExternalProject)
macro (add_host_project PROJECT_DIR)
    ExternalProject_Add(${PROJECT_DIR}
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_DIR}
            CMAKE_CACHE_ARGS
                -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/host
    )
endmacro ()

# build host tool projects
add_host_project (imager)
add_host_project (rd)
add_host_project (lzss)
add_host_project (file2c)

# build cmocka as a host project (using ExternalProject)
if (CMAKE_HOST_UNIX)
    set (CMOCKA_LIB_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/host/lib/libcmocka-static.a)
elseif(CMAKE_HOST_WIN32)
    set (CMOCKA_LIB_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/host/lib/cmocka-static.lib)
else()
    message(FATAL "missing host variable set here")
endif()
file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host/include)
ExternalProject_Add(cmocka-project
        GIT_REPOSITORY https://git.cryptomilk.org/projects/cmocka.git
        GIT_TAG        cmocka-1.1.5
        CMAKE_ARGS     -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/host -DWITH_STATIC_LIB=ON -DWITH_CMOCKERY_SUPPORT=OFF -DWITH_EXAMPLES=OFF -DUNIT_TESTING=OFF -DPICKY_DEVELOPER=OFF
        CONFIGURE_HANDLED_BY_BUILD ON
        BUILD_BYPRODUCTS ${CMOCKA_LIB_OUTPUT}
)

add_library(cmocka STATIC IMPORTED GLOBAL)
set_target_properties(cmocka PROPERTIES
        IMPORTED_LOCATION ${CMOCKA_LIB_OUTPUT}
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR}/host/include
)

# build native tool projects
set (VAFS_BUILD_TOOLS OFF CACHE BOOL "Disable vafs tools for native build")
add_subdirectory (rd)
target_include_directories(vafs-blockcache PRIVATE ../librt/libos/include ../librt/libc/include)
target_include_directories(vafs PRIVATE ../librt/libos/include ../librt/libc/include)
