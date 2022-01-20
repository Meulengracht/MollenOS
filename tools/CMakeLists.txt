cmake_minimum_required (VERSION 3.13.4)
project (ValiTools)
enable_language (C)

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Build the diskutility, since this is a c# project it is a bit tricky
add_subdirectory (diskutility)

# Build the ramdisk utility
add_subdirectory(rd)

# Build the image compressor utility
add_executable (lzss lzss/main.c)
if (MSVC)
    target_link_libraries (lzss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lzss/aplib64.lib)
else ()
    target_link_libraries (lzss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lzss/aplib64.a)
endif ()
install(TARGETS lzss EXPORT tools_lz DESTINATION bin)
install(EXPORT tools_lz NAMESPACE lz_ DESTINATION lib/tools_lz)

# Build the file to C-hex array utility
add_executable (file2c file2c/main.c)
install(TARGETS file2c EXPORT tools_f2c DESTINATION bin)
install(EXPORT tools_f2c NAMESPACE f2c_ DESTINATION lib/tools_f2c)
