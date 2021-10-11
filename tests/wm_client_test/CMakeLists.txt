if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiTest_WM_CLIENT)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../librt/libgracht/include
    ../../librt/libusb/include
    ../../librt/libddk/include
    ../../librt/libds/include
    ../../librt/libc/include
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_utils_protocol_client.c
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/librt/libgracht/tests/test_protocol.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client
    DEPENDS ${CMAKE_SOURCE_DIR}/librt/libgracht/tests/test_protocol.xml
)

set (ADDITONAL_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/test_utils_protocol_client.c)

add_test_target(gclient ""
    ${ADDITONAL_SOURCES}
    main.c
)
