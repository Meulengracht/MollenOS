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
add_dependencies(libusb libddk)
install(TARGETS libusb
    ARCHIVE DESTINATION lib/vali-ddk
    LIBRARY DESTINATION lib/vali-ddk
    RUNTIME DESTINATION bin/vali-ddk
)
