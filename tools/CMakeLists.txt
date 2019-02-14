# Configuration
enable_language (C)

# Configure compilation options
if (NOT DEFINED VALI_BUILD)
    message (FATAL_ERROR "You must invoke the root cmake file, not the individual files")
endif ()

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# target_compile_options(example PUBLIC -std=c++1y -Wall -Wfloat-conversion)
if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif ()

# Build the revision file generator utility
add_executable (revision revision/main.c)

# Build the ramdisk utility
add_executable (rd rd/main.c)

# Build the image compressor utility
add_executable (lzss lzss/main.c)
if (MSVC)
    target_link_libraries (lzss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lzss/aplib64.lib)
else ()
    target_link_libraries (lzss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lzss/aplib64.a)
endif ()

# Build the gracht protocol utility

# Build the diskutility, since this is a c# project it is a bit tricky
add_subdirectory (diskutility)

# Build the file to C-hex array utility
add_executable (file2c file2c/main.c)
