# Project setup
project (ValiUSB)
enable_language (C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ../libgracht/include
    ../libddk/include
    ../libds/include
    ../libc/include
    ../include
    ./include
)

add_library(libusb functions.c)
install(TARGETS libusb LIBRARY DESTINATION lib)
