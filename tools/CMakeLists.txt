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

# build native tool projects
set (VAFS_BUILD_TOOLS OFF CACHE BOOL "Disable vafs tools for native build")
add_subdirectory (rd)
target_include_directories(vafs PRIVATE ../librt/libc/include)
