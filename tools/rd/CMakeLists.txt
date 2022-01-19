cmake_minimum_required(VERSION 3.14.3)
project(vafs C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(libvafs)

add_executable(mkvafs
    tools/mkvafs.c
)
target_include_directories(mkvafs PRIVATE include)

target_link_libraries(mkvafs vafs)
if (UNIX)
    target_link_libraries(mkvafs -lpthread)
endif (UNIX)

add_executable(unmkvafs
    tools/unmkvafs.c
)
target_include_directories(unmkvafs PRIVATE include)

target_link_libraries(unmkvafs vafs)
if (UNIX)
    target_link_libraries(unmkvafs -lpthread)
endif (UNIX)
