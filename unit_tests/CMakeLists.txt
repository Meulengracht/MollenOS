cmake_minimum_required (VERSION 3.8.2)
project (ValiUnitTests)
enable_language (C)

macro (add_unit_test)
    if (${ARGC} GREATER_EQUAL 3)
        set (TEST_SOURCES "${ARGN}")
        list (POP_FRONT TEST_SOURCES) # target
        list (POP_FRONT TEST_SOURCES) # compile flags

        add_executable(${ARGV0} ${TEST_SOURCES})
        set_target_properties (${ARGV0}
            PROPERTIES
                COMPILE_FLAGS "${ARGV1}"
        )
        install(TARGETS ${ARGV0} DESTINATION bin)
    else ()
        message (FATAL_ERROR "Not enough parameters for add_unit_test")
    endif ()
endmacro ()

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# target_compile_options(example PUBLIC -std=c++1y -Wall -Wfloat-conversion)
if (MSVC)
    add_definitions (-D_CRT_SECURE_NO_WARNINGS)
endif ()

# Unit tests
add_unit_test (map_parser_test "-ggdb -rdynamic" map_parser_test.c)
add_unit_test (fread_tests "-ggdb -rdynamic" fread_tests.c)
