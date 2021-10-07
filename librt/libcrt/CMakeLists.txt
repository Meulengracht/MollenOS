# Project setup
project (ValiCRT)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_OBJECT_FORMAT ${NASM_DEFAULT_FORMAT})

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ../libgracht/include
    ../libusb/include
    ../libds/include
    ../libddk/include
    ../libc/include
    ../include
    ./
)

set (MSCRT_SOURCES 
    mscrt/_alldiv.s
    mscrt/_alldvrm.s
    mscrt/_aulldiv.s
    mscrt/_aulldvrm.s
    mscrt/_aullrem.s
    mscrt/_aullshr.s
    mscrt/_constants.s
    mscrt/_ints.s
    mscrt/_llmul.s
    mscrt/_llrem.s
    mscrt/_llshl.s
    mscrt/_llshr.s
)

# Add support for .s extension
set_source_files_properties(${MSCRT_SOURCES} PROPERTIES LANGUAGE ASM_NASM)

add_library(libcrt
    ${MSCRT_SOURCES}
    crt/argparse.c
    crt/crtinit.c
    crt/crtcoff.c
    crt/crtrelocs.c

    console.c
    library.c
    module.c
    service.c
)
add_dependencies(libcrt libddk)

install(TARGETS libcrt
    ARCHIVE DESTINATION vali-sdk/lib
    LIBRARY DESTINATION vali-sdk/lib
    RUNTIME DESTINATION vali-sdk/bin
)
