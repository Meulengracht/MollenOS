# Project setup
project (vali-libm)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_OBJECT_FORMAT ${NASM_DEFAULT_FORMAT})
set (MATH_FLAGS "")
set (EXTRA_SOURCES "")

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
    )
    set_source_files_properties(${ASM_SOURCES} PROPERTIES COMPILE_FLAGS "-D_MICROSOFT_LIBM")
endif ()

set_source_files_properties(${C_ARCH_SOURCES} ${C_SOURCES} ${EXTRA_SOURCES}
    PROPERTIES
        COMPILE_FLAGS "${MATH_FLAGS} -fno-builtin -fno-strict-aliasing"
)

add_library(libm STATIC
        ${ASM_SOURCES}
        ${C_ARCH_SOURCES}
        ${C_SOURCES}
        ${EXTRA_SOURCES}
)
target_include_directories(libm PUBLIC include)
target_include_directories(libm PRIVATE ../libos/include ../libc/include)

install(TARGETS libm LIBRARY
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

add_library(m SHARED
        ${ASM_SOURCES}
        ${C_ARCH_SOURCES}
        ${C_SOURCES}
        ${EXTRA_SOURCES}
)
target_include_directories(m PUBLIC include)
target_include_directories(m PRIVATE ../libos/include ../libc/include)
target_compile_definitions(m PRIVATE -D__OSLIB_C_IMPLEMENTATION)
target_link_options(m PRIVATE "LINKER:-def:${CMAKE_CURRENT_SOURCE_DIR}/${VALI_ARCH}/libm.def,-noentry")

install(TARGETS m
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
