# Project setup
project (ValiLibC)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_OBJECT_FORMAT ${NASM_DEFAULT_FORMAT})
set (MATH_FLAGS "")

if (${VALI_ARCH} STREQUAL "i386")
    set (MATH_FLAGS -D_DOUBLE_IS_32BITS -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL)
    include_directories(../../kernel/include ../../kernel/x86/x32)
elseif (${VALI_ARCH} STREQUAL "amd64")
    set (MATH_FLAGS -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL)
    include_directories(../../kernel/include ../../kernel/x86/x64)
endif ()

# Get all the source files for the arch
file(GLOB ASM_SOURCES arch/${VALI_ARCH}/*.s)

# Add support for .S extension
set_source_files_properties(${ASM_SOURCES} PROPERTIES LANGUAGE ASM_NASM)

set (SOURCES_LIBK
        time/common/month_lengths.c
        locale/locale.c
        stdio/libc_io.c
        stdlib/itoa.c
        stdlib/strtoul.c
        stdlib/abs.c
        wstring/wcsnlen.c
)

set (LIBK_TARGETS)
macro (add_libk_target)
    if (${ARGC} GREATER_EQUAL 2)
        set (LIB_SRCS "${ARGN}")
        list (POP_FRONT LIB_SRCS) # target

        add_library(${ARGV0} STATIC ${LIB_SRCS})

        # disable cpu optimizations for the kernel library as we cannot make any guarantees for
        # the hardware the kernel runs on top of.
        target_compile_options(${ARGV0} PRIVATE $<$<COMPILE_LANGUAGE:C>:-mno-sse>)

        # add various implementation flags that mark both implementation aspect and kernel aspect
        target_compile_definitions(${ARGV0} PRIVATE ${MATH_FLAGS} -D__OSLIB_C_IMPLEMENTATION -DLIBC_KERNEL -D__LIBDS_KERNEL__)
        target_include_directories(${ARGV0} PRIVATE include)

        # link against other static libraries.
        # LibDSk we must link against for data-structures
        # LibDDK we must link against for various definitions
        # GrachtStatic is to be removed from the libc project in due time
        target_link_libraries(${ARGV0} PRIVATE libdsk libddk gracht_static)

        # add it to the list of libk targets
        set (LIBK_TARGETS ${LIBK_TARGETS} ${ARGV0} PARENT_SCOPE)
    else ()
        message (FATAL_ERROR "usage: add_libk_target <target> <source files>")
    endif ()
endmacro ()

set (LIBC_STATIC_OBJS)
set (LIBC_SHARED_OBJS)
macro (add_libc_target)
    if (${ARGC} GREATER_EQUAL 2)
        set (LIB_SRCS "${ARGN}")
        list (POP_FRONT LIB_SRCS) # target

        add_library(${ARGV0} OBJECT ${LIB_SRCS})

        # add various implementation flags that mark implementation aspect for the shared
        # build.
        target_compile_definitions(${ARGV0} PRIVATE ${MATH_FLAGS} -D__OSLIB_C_IMPLEMENTATION)
        target_include_directories(${ARGV0} PRIVATE include)
        target_link_libraries(${ARGV0} PRIVATE libos libm libds libddk gracht_static)

        # add it to the list of libc static and shared targets
        set (LIBC_STATIC_OBJS ${LIBC_STATIC_OBJS} $<TARGET_OBJECTS:${ARGV0}> PARENT_SCOPE)
        set (LIBC_SHARED_OBJS ${LIBC_SHARED_OBJS} $<TARGET_OBJECTS:${ARGV0}> PARENT_SCOPE)
    else ()
        message (FATAL_ERROR "usage: add_libc_target <target> <source files>")
    endif ()
endmacro ()

# add subprojects
add_subdirectory(assert)
add_subdirectory(ctype)
add_subdirectory(dl)
add_subdirectory(errno)
add_subdirectory(getopt)
add_subdirectory(locale)
add_subdirectory(mem)
add_subdirectory(os)
add_subdirectory(signal)
add_subdirectory(stdio)
add_subdirectory(stdlib)
add_subdirectory(string)
add_subdirectory(threads)
add_subdirectory(time)
add_subdirectory(wstring)

# The kernel (static) build of libc should contain the absolutely bare minimum required for the
# kernel to run. This is compiled without any cpu accelleration, as we can't make any guarantees
# of the cpu it will run on top of. The *ONLY* purpose of this is to be linked into the kernel, and
# nothing but the kernel executable should link against this.
add_library(libk STATIC ${ASM_SOURCES} ${SOURCES_LIBK})
target_compile_options(libk PRIVATE $<$<COMPILE_LANGUAGE:C>:-mno-sse>)
target_compile_definitions(libk PRIVATE ${MATH_FLAGS} -D__OSLIB_C_IMPLEMENTATION -DLIBC_KERNEL -D__LIBDS_KERNEL__)
target_include_directories(libk PUBLIC include)
target_include_directories(libk PRIVATE ../libos/include)
target_link_libraries(libk PRIVATE libdsk libddk gracht_static)
target_link_libraries(libk PUBLIC ${LIBK_TARGETS})

# The static build of libc exists *SOLELY* for the purpose of phoenix. Phoenix is the bootstrap
# process in Vali, which is bundled with the initfs, and can thus not load any dependencies. This
# means its a single binary blob, and thus needs *ALL* libraries linked in. This should NEVER be
# used as a replacement for the regular, dynamic libc as this contains code that should exist only
# once per process. Until we seperate this code out into its own library, libc must be loaded as a
# dynamic library and shared between all running libraries in a single process-space.
add_library(libc STATIC ${ASM_SOURCES} ${LIBC_STATIC_OBJS})
target_compile_definitions(libc PRIVATE ${MATH_FLAGS} -D__OSLIB_C_IMPLEMENTATION)
target_include_directories(libc PUBLIC include)
target_link_libraries(libc PUBLIC libos libm libds libddk gracht_static)

add_library(c SHARED ${ASM_SOURCES} ${LIBC_SHARED_OBJS} main.c)
target_compile_definitions(c PRIVATE ${MATH_FLAGS} -D__OSLIB_C_IMPLEMENTATION)
target_include_directories(c PUBLIC include)
target_link_options(c PRIVATE "LINKER:-def:${CMAKE_CURRENT_SOURCE_DIR}/libc.def")
target_link_libraries(c PRIVATE libos gracht_static libds libddk libm librt)

# create an interface library for libc so targets can get the include files
# without actually including either the static or shared library copy
add_library(libc-api INTERFACE)
target_include_directories(libc-api INTERFACE include)

install(TARGETS c
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "internal" EXCLUDE
)

# make sure the C library is deployed as the services/modules depend on it
deploy_target_to_initrd(c)
