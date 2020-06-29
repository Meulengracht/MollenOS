# Project setup
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
        ../libds/include
        ../libddk/include
        ../libc/include
        ../include
        ./include
    )
else ()
    include_directories(./include)
endif ()

if (MOLLENOS)
    add_sources (os/vali/client.c os/vali/message.c os/vali/os.c os/vali/server.c)
endif ()

add_sources(link/client.c link/server.c)
add_sources(client.c crc16.c server.c shared.c)

add_library(libgracht ${SRCS})
install(TARGETS libgracht
    ARCHIVE DESTINATION vali-ddk/lib
    LIBRARY DESTINATION vali-ddk/lib
    RUNTIME DESTINATION vali-ddk/bin
)
install(DIRECTORY include/ DESTINATION vali-ddk/include)
install(DIRECTORY generator/ DESTINATION vali-ddk/share/vali)

if (GRACHT_BUILD_TESTS)
    add_custom_command(
        OUTPUT  test_utils_protocol_server.c test_utils_protocol_client.c
        COMMAND python generator/parser.py --protocol tests/test_protocol.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --server
        COMMAND python generator/parser.py --protocol tests/test_protocol.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client
        DEPENDS tests/test_protocol.xml
    )

    add_executable(gclient
        tests/client/main.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/test_utils_protocol_client.c
    )

    add_executable(gserver
        tests/server/main.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/test_utils_protocol_server.c
    )

    if (UNIX)
        target_link_libraries(gclient -lrt -lc)
        target_link_libraries(gserver -lrt -lc)
    endif ()
endif ()
