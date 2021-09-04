# Project setup
project (ValiUSB)
enable_language (C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ../libgracht/include
    ../libddk/include
    ../libds/include
    ../libc/include
    ../include
    ./include
)

add_library(libusb functions.c)
add_dependencies(libusb libddk)

target_include_directories(libusb
    PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/vali-ddk/include>"
)

install(TARGETS libusb
    EXPORT ddk_targets
    ARCHIVE DESTINATION vali-ddk/lib
    LIBRARY DESTINATION vali-ddk/lib
    RUNTIME DESTINATION vali-ddk/bin
    INCLUDES DESTINATION vali-ddk/include
)
