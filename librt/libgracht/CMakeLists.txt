# Project setup
if (NOT VALI_BUILD)
    cmake_minimum_required (VERSION 3.8.2)
endif ()

project (Gracht)
enable_language (C)

option (GRACHT_BUILD_TESTS "Build test server and client program for gracht" OFF)
option (GRACHT_INSTALL_HEADERS "Install headers and library")

set (SRCS "")

macro (add_sources)
    file (RELATIVE_PATH _relPath "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach (_src ${ARGN})
        if (_relPath)
            list (APPEND SRCS "${_relPath}/${_src}")
        else()
            list (APPEND SRCS "${_src}")
        endif()
    endforeach()
endmacro()

# Configure include paths
if (VALI_BUILD)
    include_directories (
        ${CMAKE_BINARY_DIR}
        ../libds/include
        ../libddk/include
        ../libc/include
        ../include
        ./include
    )
else ()
    include_directories(
        ${CMAKE_BINARY_DIR}
        ./include
    )
endif ()

if (MOLLENOS)
    add_sources (
            link/vali-ipc/client.c
            link/vali-ipc/message.c
            link/vali-ipc/os.c
            link/vali-ipc/server.c
    )
endif ()

add_sources(link/socket/client.c link/socket/server.c)
add_sources(client.c crc.c server.c shared.c)

add_library(libgracht ${SRCS})

if (VALI_BUILD)
    install(TARGETS libgracht
        ARCHIVE DESTINATION vali-ddk/lib
        LIBRARY DESTINATION vali-ddk/lib
        RUNTIME DESTINATION vali-ddk/bin
    )
    install(DIRECTORY include/ DESTINATION vali-ddk/include)
    install(DIRECTORY generator/ DESTINATION vali-ddk/share/vali)
else ()
    install(TARGETS libgracht
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
    install(DIRECTORY include/ DESTINATION include)
    install(DIRECTORY generator/ DESTINATION share/gracht)
    install(FILES protocol.xml DESTINATION share/gracht)
endif ()

if (GRACHT_BUILD_TESTS)
    add_custom_command(
        OUTPUT  test_utils_protocol_server.c test_utils_protocol_client.c
        COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/generator/parser.py --protocol ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_protocol.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --server --client
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_protocol.xml
    )

    add_executable(gclient
        tests/client/main.c
        ${CMAKE_CURRENT_BINARY_DIR}/test_utils_protocol_client.c
    )
    target_link_libraries(gclient libgracht)

    add_executable(gserver
        tests/server/main.c
        ${CMAKE_CURRENT_BINARY_DIR}/test_utils_protocol_server.c
    )
    target_link_libraries(gserver libgracht)

    if (UNIX)
        target_link_libraries(gclient -lrt -lc -lpthread)
        target_link_libraries(gserver -lrt -lc -lpthread)
    endif ()
endif ()
