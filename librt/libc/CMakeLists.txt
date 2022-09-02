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

set (SOURCES_ASSERT
    assert/assert.c
)

set (SOURCES_ERRNO
    errno/errno.c
    errno/errstrings.c
)

set (SOURCES_MEMORY
    mem/memchr.c
    mem/memcmp.c
    mem/memcpy.c
    mem/memmove.c
    mem/memset.c
)

set (SOURCES_STRING
    string/strcasecmp.c
    string/strcat.c
    string/strchr.c
    string/strcmp.c
    string/strcoll.c
    string/strcpy.c
    string/strcspn.c
    string/strdup.c
    string/strlen.c
    string/strncasecmp.c
    string/strncat.c
    string/strncmp.c
    string/strncpy.c
    string/strnlen.c
    string/strpbrk.c
    string/strrchr.c
    string/strspn.c
    string/strstr.c
    string/strtok.c
    string/strxfrm.c
)

set (SOURCES_CTYPE_COMMON
    ctype/common/ctype_.c
    ctype/common/isalnum.c
    ctype/common/isalpha.c
    ctype/common/isascii.c
    ctype/common/isblank.c
    ctype/common/iscntrl.c
    ctype/common/isdigit.c
    ctype/common/islower.c
    ctype/common/isprint.c
    ctype/common/ispunct.c
    ctype/common/isspace.c
    ctype/common/isupper.c
    ctype/common/isxdigit.c
    ctype/common/toascii.c
    ctype/common/tolower.c
    ctype/common/toupper.c
)

set (SOURCES_STDIO_COMMON
    stdio/stream/_sxprintf.c
    stdio/stream/asprintf.c
    stdio/stream/fprintf.c
    stdio/stream/fwprintf.c
    stdio/stream/printf.c
    stdio/stream/snprintf.c
    stdio/stream/sprintf.c
    stdio/stream/streamout.c
    stdio/stream/swnprintf.c
    stdio/stream/swprintf.c
    stdio/stream/vasprintf.c
    stdio/stream/vfprintf.c
    stdio/stream/vfwprintf.c
    stdio/stream/vprintf.c
    stdio/stream/vsnprintf.c
    stdio/stream/vsprintf.c
    stdio/stream/vswprintf.c
    stdio/stream/vwprintf.c
    stdio/stream/wprintf.c
    stdio/stream/wstreamout.c
)

set (SOURCES_TIME_COMMON
    time/common/month_lengths.c
)

set (SOURCES_CTYPE 
    ${SOURCES_CTYPE_COMMON}
    ctype/isalnum_l.c
    ctype/isalpha_l.c
    ctype/isascii_l.c
    ctype/isblank_l.c
    ctype/iscntrl_l.c
    ctype/isdigit_l.c
    ctype/islower_l.c
    ctype/isprint_l.c
    ctype/ispunct_l.c
    ctype/isspace_l.c
    ctype/isupper_l.c
    ctype/iswalnum.c
    ctype/iswalnum_l.c
    ctype/iswalpha.c
    ctype/iswalpha_l.c
    ctype/iswblank.c
    ctype/iswblank_l.c
    ctype/iswcntrl.c
    ctype/iswcntrl_l.c
    ctype/iswctype.c
    ctype/iswctype_l.c
    ctype/iswdigit.c
    ctype/iswdigit_l.c
    ctype/iswgraph.c
    ctype/iswgraph_l.c
    ctype/iswlower.c
    ctype/iswlower_l.c
    ctype/iswprint.c
    ctype/iswprint_l.c
    ctype/iswpunct.c
    ctype/iswpunct_l.c
    ctype/iswspace.c
    ctype/iswspace_l.c
    ctype/iswupper.c
    ctype/iswupper_l.c
    ctype/iswxdigit.c
    ctype/iswxdigit_l.c
    ctype/isxdigit_l.c
    ctype/jp2uc.c
    ctype/toascii_l.c
    ctype/tolower_l.c
    ctype/toupper_l.c
    ctype/towctrans.c
    ctype/towctrans_l.c
    ctype/towlower.c
    ctype/towlower_l.c
    ctype/towupper.c
    ctype/towupper_l.c
    ctype/wctrans.c
    ctype/wctrans_l.c
    ctype/wctype.c
    ctype/wctype_l.c
)

set (SOURCES_DL
    dl/dlfcn.c
)

set (SOURCES_GETOPT
    getopt/getopt_long.c
)

set (SOURCES_LOCALE
    locale/duplocale.c
    locale/freelocale.c
    locale/lctype.c
    locale/lmessages.c
    locale/lmonetary.c
    locale/lnumeric.c
    locale/locale.c
    locale/localeconv.c
    locale/newlocale.c
    locale/timelocal.c
    locale/uselocale.c
)

set (SOURCES_SIGNAL
    signal/signal.c
)

set (SOURCES_STDIO
    ${SOURCES_STDIO_COMMON}
    stdio/file/dirent.c
    stdio/file/link.c
    stdio/file/open.c
    stdio/file/remove.c
    stdio/file/rename.c
    stdio/file/tmpnam.c
    stdio/file/tmpfile.c
 
    stdio/inet/_socket.c
    stdio/inet/accept.c
    stdio/inet/bind.c
    stdio/inet/connect.c
    stdio/inet/getpeername.c
    stdio/inet/getsockname.c
    stdio/inet/getsockopt.c
    stdio/inet/listen.c
    stdio/inet/recv.c
    stdio/inet/send.c
    stdio/inet/setsockopt.c
    stdio/inet/shutdown.c
    stdio/inet/socket.c
    stdio/inet/socketatmark.c
    stdio/inet/socketpair.c

    stdio/io/chsize.c
    stdio/io/clearerr.c
    stdio/io/convflags.c
    stdio/io/dup.c
    stdio/io/fclose.c
    stdio/io/feof.c
    stdio/io/ferror.c
    stdio/io/fflush.c
    stdio/io/fgetc.c
    stdio/io/fgetchar.c
    stdio/io/fgetpos.c
    stdio/io/fgets.c
    stdio/io/fgetwc.c
    stdio/io/fgetwchar.c
    stdio/io/fgetws.c
    stdio/io/fileno.c
    stdio/io/flsbuf.c
    stdio/io/flswbuf.c
    stdio/io/fopen.c
    stdio/io/fputc.c
    stdio/io/fputchar.c
    stdio/io/fputs.c
    stdio/io/fputwc.c
    stdio/io/fputwchar.c
    stdio/io/fputws.c
    stdio/io/fread.c
    stdio/io/freopen.c
    stdio/io/fseek.c
    stdio/io/fsetpos.c
    stdio/io/ftell.c
    stdio/io/fungetc.c
    stdio/io/fwide.c
    stdio/io/fwrite.c
    stdio/io/getc.c
    stdio/io/getchar.c
    stdio/io/gets.c
    stdio/io/getsw.c
    stdio/io/getw.c
    stdio/io/getwc.c
    stdio/io/getwchar.c
    stdio/io/ioctl.c
    stdio/io/ioset.c
    stdio/io/lock.c
    stdio/io/marktty.c
    stdio/io/perror.c
    stdio/io/putc.c
    stdio/io/putchar.c
    stdio/io/puts.c
    stdio/io/putsw.c
    stdio/io/putw.c
    stdio/io/putwc.c
    stdio/io/putwch.c
    stdio/io/putwchar.c
    stdio/io/rewind.c
    stdio/io/scanf.c
    stdio/io/setbuf.c
    stdio/io/setvbuf.c
    stdio/io/strerror.c
    stdio/io/ungetc.c
    stdio/io/ungetwc.c

    stdio/pipe/pipe.c

    stdio/libc_io.c
    stdio/libc_io_bitmap.c
    stdio/libc_io_buffered.c
    stdio/libc_io_stream.c

    stdio/libc_io_set_operations.c
    stdio/libc_io_file_operations.c
    stdio/libc_io_ipc_operations.c
    stdio/libc_io_net_operations.c
    stdio/libc_io_null_operations.c
    stdio/libc_io_pipe_operations.c
    stdio/libc_io_evt_operations.c
)

set (SOURCES_STDLIB
    stdlib/mb/mblen.c
    stdlib/mb/mblen_r.c
    stdlib/mb/mbrlen.c
    stdlib/mb/mbrtowc.c
    stdlib/mb/mbsinit.c
    stdlib/mb/mbsnrtowcs.c
    stdlib/mb/mbsrtowcs.c
    stdlib/mb/mbstowcs.c
    stdlib/mb/mbtowc.c
    
    stdlib/wide/wcrtomb.c
    stdlib/wide/wcsnrtombs.c
    stdlib/wide/wcsrtombs.c
    stdlib/wide/wcstod.c
    stdlib/wide/wcstoimax.c
    stdlib/wide/wcstol.c
    stdlib/wide/wcstold.c
    stdlib/wide/wcstoll.c
    stdlib/wide/wcstombs.c
    stdlib/wide/wcstoul.c
    stdlib/wide/wcstoull.c
    stdlib/wide/wcstoumax.c
    stdlib/wide/wctob.c
    stdlib/wide/wctomb.c

        stdlib/abort.c
    stdlib/abs.c
    stdlib/aligned_alloc.c
    stdlib/atof.c
    stdlib/atoff.c
    stdlib/atoi.c
    stdlib/atol.c
    stdlib/atold.c
    stdlib/atoll.c
    stdlib/bsearch.c
    stdlib/btowc.c
    stdlib/div.c
    stdlib/exit.c
    stdlib/gdtoa.c
    stdlib/gdtoa_hex.c
    stdlib/getenv.c
    stdlib/imaxabs.c
    stdlib/imaxdiv.c
    stdlib/itoa.c
    stdlib/labs.c
    stdlib/ldiv.c
    stdlib/llabs.c
    stdlib/lldiv.c
    stdlib/malloc.c
    stdlib/mprec.c
    stdlib/qsort.c
    stdlib/rand.c
    stdlib/strtod.c
    stdlib/strtodg.c
    stdlib/strtoimax.c
    stdlib/strtol.c
    stdlib/strtold.c
    stdlib/strtoll.c
    stdlib/strtorx.c
    stdlib/strtoul.c
    stdlib/strtoull.c
    stdlib/strtoumax.c
    stdlib/system.c
)

set (SOURCES_OS
    os/synchronization/binarysemaphore.c
    os/synchronization/spinlock.c
    os/synchronization/event.c

    os/scheduler/scheduler.c
    os/scheduler/mutex.c
    os/scheduler/cond.c
    os/scheduler/xunit.c
    os/scheduler/tls.c

    os/keymaps/en-us.c

    os/clang.c
    os/dmabuf.c
    os/error_codes.c
    os/file.c
    os/init.c
    os/ipcontext.c
    os/keycodes.c
    os/memory.c
    os/path.c
    os/process.c
    os/sha1.c
    os/shared_objects.c
    os/syscalls.c
    os/system.c
    os/time.c
    os/thread_helpers.c
    os/unwind.c
)

set (SOURCES_THREADS
    threads/condition.c
    threads/mutex.c
    threads/thread.c
        threads/tss.c
)

set (SOURCES_TIME
        ${SOURCES_TIME_COMMON}
        time/asctime.c
        time/clock.c
        time/clock_getfreq.c
        time/ctime.c
        time/difftime.c
        time/gettzinfo.c
        time/gmtime.c
        time/lcltime.c
        time/mktime.c
        time/strftime.c
        time/time.c
        time/timespec_get.c
        time/timespec_getres.c
        time/timespec_diff.c
        time/tzcalc_limits.c
        time/tzlock.c
        time/tzset.c
        time/tzset_r.c
        time/wcsftime.c
)

set (SOURCES_WSTRING
    wstring/wcpcpy.c
    wstring/wcpncpy.c
    wstring/wcscasecmp.c
    wstring/wcscasecmp_l.c
    wstring/wcscat.c
    wstring/wcschr.c
    wstring/wcscmp.c
    wstring/wcscoll.c
    wstring/wcscoll_l.c
    wstring/wcscpy.c
    wstring/wcscspn.c
    wstring/wcsdup.c
    wstring/wcslcat.c
    wstring/wcslcpy.c
    wstring/wcslen.c
    wstring/wcsncasecmp.c
    wstring/wcsncasecmp_l.c
    wstring/wcsncat.c
    wstring/wcsncmp.c
    wstring/wcsncpy.c
    wstring/wcsnlen.c
    wstring/wcspbrk.c
    wstring/wcsrchr.c
    wstring/wcsspn.c
    wstring/wcsstr.c
    wstring/wcstok.c
    wstring/wcswidth.c
    wstring/wcsxfrm.c
    wstring/wcsxfrm_l.c
    wstring/wcwidth.c
    wstring/wmemchr.c
    wstring/wmemcmp.c
    wstring/wmemcpy.c
    wstring/wmemmove.c
    wstring/wmemset.c
)

set (SOURCES_LIBK
    ${SOURCES_ASSERT}
    ${SOURCES_ERRNO}
    ${SOURCES_MEMORY}
    ${SOURCES_STRING}
    ${SOURCES_CTYPE_COMMON}
    ${SOURCES_STDIO_COMMON}
    ${SOURCES_TIME_COMMON}

    os/synchronization/spinlock.c
    locale/locale.c
    stdio/libc_io.c
    stdlib/itoa.c
    stdlib/strtoul.c
    stdlib/abs.c
    wstring/wcsnlen.c
)

set (SOURCES_LIBC
    ${SOURCES_ASSERT}
    ${SOURCES_ERRNO}
    ${SOURCES_MEMORY}
    ${SOURCES_STRING}
    ${SOURCES_CTYPE}
    ${SOURCES_DL}
    ${SOURCES_GETOPT}
    ${SOURCES_LOCALE}
    ${SOURCES_SIGNAL}
    ${SOURCES_STDIO}
    ${SOURCES_STDLIB}
    ${SOURCES_THREADS}
    ${SOURCES_TIME}
    ${SOURCES_OS}
    ${SOURCES_WSTRING}
)

add_library(libk STATIC ${ASM_SOURCES} ${SOURCES_LIBK})
target_compile_options(libk PRIVATE $<$<COMPILE_LANGUAGE:C>:-mno-sse>)
target_compile_definitions(libk PRIVATE ${MATH_FLAGS} -D__OSLIB_C_IMPLEMENTATION -DLIBC_KERNEL -D__LIBDS_KERNEL__ -D_KRNL_DLL)
target_include_directories(libk PUBLIC include)
target_include_directories(libk PRIVATE ../libos/include)
target_link_libraries(libk PRIVATE libdsk libddk gracht_static)

add_library(libc STATIC ${ASM_SOURCES} ${SOURCES_LIBC})
target_compile_definitions(libc PRIVATE ${MATH_FLAGS} -D__OSLIB_C_IMPLEMENTATION -D_KRNL_DLL)
target_include_directories(libc PUBLIC include)
target_include_directories(libc PRIVATE ../libos/include)
target_link_libraries(libc PRIVATE libm libds libddk gracht_static)

add_library(c SHARED ${ASM_SOURCES} ${SOURCES_LIBC} main.c)
target_compile_options(c
  PRIVATE $<$<COMPILE_LANGUAGE:C>:${MATH_FLAGS} -D__OSLIB_C_IMPLEMENTATION -D__OSLIB_C_SHAREDLIBRARY -DCRTDLL>
)
target_include_directories(c PUBLIC include)
target_include_directories(c PRIVATE ../libos/include)
target_link_options(c PRIVATE "LINKER:-def:${CMAKE_CURRENT_SOURCE_DIR}/libc.def")
target_link_libraries (c PRIVATE gracht_static libds libddk libm librt)

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
