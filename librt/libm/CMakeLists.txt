# Project setup
project (ValiMath)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_OBJECT_FORMAT ${NASM_DEFAULT_FORMAT})
set (MATH_FLAGS "")
set (EXTRA_SOURCES "")

# Configure include paths
include_directories (
    ../libc/include
    ../include
    ./include
)

# Get all the source files for the arch
file(GLOB ASM_SOURCES ${VALI_ARCH}/*.s)
file(GLOB C_ARCH_SOURCES ${VALI_ARCH}/*.c)
file(GLOB C_SOURCES ./*.c)

# Add support for .S extension
set_source_files_properties(${ASM_SOURCES} PROPERTIES LANGUAGE ASM_NASM)

if (${VALI_ARCH} STREQUAL "i386")
    set (MATH_FLAGS "-D_DOUBLE_IS_32BITS -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL")
    set (EXTRA_SOURCES
        ld80/lgammal_r.c
        ld80/tgammal.c
        ld80/expl.c
        ld80/powl.c
        ld80/core_cosl.c
        ld80/core_sinl.c
        ld80/core_tanl.c
        ld80/sinl.c
        ld80/cosl.c
    )
elseif (${VALI_ARCH} STREQUAL "amd64")
    set (MATH_FLAGS "-D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL")
    set (EXTRA_SOURCES
        ld80/lgammal_r.c
        ld80/tgammal.c
        ld80/expl.c
        ld80/powl.c
        ld80/core_cosl.c
        ld80/core_sinl.c
        ld80/core_tanl.c
        ld80/logl.c
        ld80/log10l.c
        ld80/sinl.c
        ld80/cosl.c
        ld80/floorl.c
        ld80/truncl.c
        ld80/ceill.c
    )
    set_source_files_properties(${ASM_SOURCES} PROPERTIES COMPILE_FLAGS "-D_MICROSOFT_LIBM")
endif ()

set_source_files_properties(${C_ARCH_SOURCES} ${C_SOURCES} ${EXTRA_SOURCES}
    PROPERTIES
        COMPILE_FLAGS "${MATH_FLAGS} -fno-builtin -fno-strict-aliasing"
)

add_library(libm ${ASM_SOURCES} ${C_ARCH_SOURCES} ${C_SOURCES} ${EXTRA_SOURCES})
install(TARGETS libm LIBRARY
    ARCHIVE DESTINATION vali-sdk/lib
    LIBRARY DESTINATION vali-sdk/lib
    RUNTIME DESTINATION vali-sdk/bin
)

add_library(m SHARED ${ASM_SOURCES} ${C_ARCH_SOURCES} ${C_SOURCES} ${EXTRA_SOURCES})
set_target_properties(m
    PROPERTIES
        COMPILE_FLAGS "-D__OSLIB_C_IMPLEMENTATION -D__OSLIB_C_SHAREDLIBRARY -DCRTDLL"
        LINK_FLAGS "/lldmap /def:${CMAKE_CURRENT_SOURCE_DIR}/${VALI_ARCH}/libm.def /noentry"
        #LINK_LIBRARIES ""
        #VERSION ${PROJECT_VERSION}
        #PUBLIC_HEADER include/mylib.h
)

install(TARGETS m
    ARCHIVE DESTINATION vali-sdk/lib
    LIBRARY DESTINATION vali-sdk/lib
    RUNTIME DESTINATION vali-sdk/bin
)
install(DIRECTORY include/ DESTINATION vali-sdk/include)
