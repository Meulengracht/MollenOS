# Project setup
project (ValiUSB)
enable_language (C)

add_library(libusb functions.c)
target_link_libraries(libusb PUBLIC libddk)

target_include_directories(libusb PRIVATE ../libc/include ../libds/include ../libgracht/include)
target_include_directories(libusb
    PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include>"
)

install(TARGETS libusb
    EXPORT ddk_targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
