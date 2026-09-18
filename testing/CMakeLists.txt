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

add_library(locale_test OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/locale_test.c
    ${CMAKE_SOURCE_DIR}/librt/libc/ctype/common/ctype_.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/duplocale.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/freelocale.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/lctype.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/lmessages.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/lmonetary.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/lnumeric.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/locale.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/localeconv.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/newlocale.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/timelocal.c
    ${CMAKE_SOURCE_DIR}/librt/libc/locale/uselocale.c
    ${CMAKE_SOURCE_DIR}/librt/libc/stdlib/getenv.c
    ${CMAKE_SOURCE_DIR}/librt/libc/stdlib/mb/mbtowc.c
    ${CMAKE_SOURCE_DIR}/librt/libc/stdlib/wide/wctomb.c)
target_compile_definitions(locale_test PRIVATE _MB_CAPABLE __OSLIB_C_IMPLEMENTATION)
target_include_directories(locale_test PRIVATE
        ${CMAKE_SOURCE_DIR}/librt/libc/include
        ${CMAKE_SOURCE_DIR}/librt/libddk/include
        ${CMAKE_SOURCE_DIR}/librt/libos/include)

configure_file(run-tests.sh ${CMAKE_BINARY_DIR}/run-tests.sh COPYONLY)
