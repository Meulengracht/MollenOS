include(FetchContent)
FetchContent_Declare(
        cmocka
        GIT_REPOSITORY https://gitlab.com/cmocka/cmocka.git
        GIT_TAG        f5e2cd77c88d9f792562888d2b70c5a396bfbf7a #cmocka-1.1.5
)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(WITH_STATIC_LIB OFF)
set(WITH_CMOCKERY_SUPPORT OFF)
set(WITH_EXAMPLES OFF)
set(UNIT_TESTING OFF)
set(PICKY_DEVELOPER OFF)
FetchContent_MakeAvailable(cmocka)

    add_executable(hid_parser_test
            ${CMAKE_CURRENT_SOURCE_DIR}/hid_parser_test.c
            ${CMAKE_SOURCE_DIR}/modules/input/hid/report_parser.c)
    target_include_directories(hid_parser_test PRIVATE
            ${CMAKE_SOURCE_DIR}/testing/include
            ${CMAKE_SOURCE_DIR}/modules/input/hid
            ${CMAKE_SOURCE_DIR}/librt/libddk/include
            ${CMAKE_SOURCE_DIR}/librt/libusb/include
            ${CMAKE_SOURCE_DIR}/librt/libos/include
            ${CMAKE_SOURCE_DIR}/librt/libds/include
            ${CMAKE_SOURCE_DIR}/boot/include)
    target_link_libraries(hid_parser_test PRIVATE cmocka)
    add_test(NAME hid_parser_test COMMAND hid_parser_test)
configure_file(run-tests.sh ${CMAKE_BINARY_DIR}/run-tests.sh COPYONLY)
