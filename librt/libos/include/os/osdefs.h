/**
 * Copyright 2011, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __OS_DEFINITIONS__
#define __OS_DEFINITIONS__

#include <crtdefs.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

/**
 * Include some C11 headers if the environment supports it.
 */
#if !defined(__cplusplus)
#if defined __STDC_VERSION__ && __STDC_VERSION__ >= 201112L
#include <stdatomic.h>
#include <stdnoreturn.h>
#include <stdbool.h>
#endif // __STDC_VERSION__ >= 201112L
#endif // !__cplusplus

// TODO move this to kernel.h
#define DECL_STRUCT(Type) typedef struct Type Type##_t

/**
 * Define _T and TCHAR types to introduce some form of compatability with other prograns
 */
#ifdef _UNICODE
# define TCHAR wchar_t
# define _T(x) L ##x
#else
# define TCHAR char
# define _T(x) x
#endif

/**
 * Define various memory types that are based on the underlying platform.
 * Introduce __BITS and __MASK to allow programs and easy way to detect 32/64 bits
 * TODO move paddr_t/vaddr_t to kernel.h
 */
#if defined(i386) || defined(__i386__)
#define __BITS   32
#define __MASK   0xFFFFFFFF
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;
#elif defined(__x86_64__) || defined(amd64) || defined(__amd64__)
#define __BITS   64
#define __MASK   0xFFFFFFFFFFFFFFFF
typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;
#endif

/**
 * Operation System Types 
 * these are usually fixed no matter arch and include stuff as threading, processing etc 
 */
typedef unsigned int irqstate_t; // TODO move this to kernel.h
typedef unsigned int uuid_t;
typedef void*        Handle_t;

#define UUID_INVALID 0

#define HANDLE_INVALID (Handle_t)0
#define HANDLE_GLOBAL  (Handle_t)1

typedef enum oserr {
    OS_EOK = 0,
    OS_EUNKNOWN,            // Unknown error ocurred
    OS_EEXISTS,             // Resource already exists
    OS_ENOENT,              // Resource does not exist
    OS_EINVALPARAMS,        // Bad parameters given
    OS_EPERMISSIONS,        // Bad permissions
    OS_ETIMEOUT,            // Operation timeout
    OS_EINTERRUPTED,        // Operation was interrupted
    OS_ENOTSUPPORTED,       // Feature/Operation not supported
    OS_EOOM,                // Out of memory
    OS_EBUSY,               // Resource is busy or contended
    OS_EINCOMPLETE,         // Operation only completed partially
    OS_ECANCELLED,          // Operation was cancelled
    OS_EBLOCKED,            // Operating was blocked
    OS_EINPROGRESS,         // Operation was in progress
    OS_ESCSTARTED,          // Syscall has been started
    OS_EFORKED,             // Thread is now running in forked context
    OS_EOVERFLOW,           // Stack has overflown

    OS_ENOTDIR,             // Path is not a directory
    OS_EISDIR,              // Path is a directory
    OS_ELINKINVAL,          // Link is invalid
    OS_ELINKS,              // Links are blocking the operation
    OS_EDIRNOTEMPTY,        // Directory is not empty
    OS_EDEVFAULT,           // Device error occurred during operation
    
    OS_EPROTOCOL,           // Protocol was invalid
    OS_ECONNREFUSED,        // Connection was refused
    OS_ECONNABORTED,        // Connection was aborted
    OS_EHOSTUNREACHABLE,    // Host could not be reached
    OS_ENOTCONNECTED,       // Not connected
    OS_EISCONNECTED,        // Already connected
    
    __OS_ECOUNT
} oserr_t;

typedef union Integer64 {
    struct {
        uint32_t LowPart;
        int32_t  HighPart;
    } s;
    struct {
        uint32_t LowPart;
        uint32_t HighPart;
    } u;
    int64_t QuadPart;
} Integer64_t;

typedef union UInteger64 {
    struct {
        uint32_t LowPart;
        int32_t  HighPart;
    } s;
    struct {
        uint32_t LowPart;
        uint32_t HighPart;
    } u;
    uint64_t QuadPart;
} UInteger64_t;

static inline int
FirstSetBit(size_t value)
{
    int    bitCount = 0;
    size_t data     = value;

    for (; data != 0;) {
        bitCount++;
        data >>= 1;
    }
    return bitCount;
}

static inline int
LastSetBit(size_t value)
{
    size_t data     = value;
    int    bitIndex = 0;

    while (data >>= 1) {
        bitIndex++;
    }
    return bitIndex;
}

static inline int
IsPowerOfTwo(size_t value)
{
    if (value == 0) {
        return 0;
    }
   
    while (value != 1) {
        if (value & 0x1) {
            return 0;
        }
        value >>= 1;
    }
    return 1;    
}

static inline size_t NextPowerOfTwo(size_t value) {
    size_t next = 1;
    if (value >> (__BITS - 1) == 1) {
        return value;
    }
    while (next < value) {
        next <<= 1;
    }
    return next;
}

#ifndef _MAXPATH
#define _MAXPATH 512
#endif //!_MAXPATH

#ifndef NAME_MAX
#define NAME_MAX 4096
#endif //!NAME_MAX

#define ISINRANGE(val, min, max)                (((val) >= (min)) && ((val) < (max)))
#define DIVUP(a, b)                             (((a) + ((b) - 1)) / (b))
#define ADDLIMIT(Base, Current, Step, Limit)    (((Current) + (Step)) >= (Limit)) ? (Base) : ((Current) + (Step))
#define SIZEOF_ARRAY(Array)                     (sizeof(Array) / sizeof((Array)[0]))
#define BOCHSBREAK                              __asm__ __volatile__ ("xchg %bx, %bx\n\t");

#ifdef __need_minmax
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#ifdef __need_static_assert
#define STATIC_ASSERT(COND,MSG)                 typedef char static_assertion_##MSG[(!!(COND))*2-1]
#define COMPILE_TIME_ASSERT3(X,L)               STATIC_ASSERT(X,static_assertion_at_line_##L)
#define COMPILE_TIME_ASSERT2(X,L)               COMPILE_TIME_ASSERT3(X,L)
#define COMPILE_TIME_ASSERT(X)                  COMPILE_TIME_ASSERT2(X,__LINE__)
#endif

#define FSEC_PER_NSEC 1000000L // Femptoseconds per nanosecond
#define FSEC_PER_SEC  1000000000000000ULL
#define NSEC_PER_USEC 1000L    // Nanoseconds per microsecond
#define NSEC_PER_MSEC 1000000L // Nanoseconds per millisecond
#define NSEC_PER_SEC  1000000000LL
#define USEC_PER_MSEC 1000L
#define USEC_PER_SEC  1000000L
#define MSEC_PER_SEC  1000L

#ifdef __need_quantity
#define BYTES_PER_KB 1024
#define BYTES_PER_MB (BYTES_PER_KB * 1024)
#define BYTES_PER_GB (BYTES_PER_MB * 1024)
#define KB(x)        ((x) * BYTES_PER_KB)
#define MB(x)        ((x) * BYTES_PER_MB)
#define GB(x)        ((x) * BYTES_PER_GB)
#endif

#ifndef LOWORD
#define LOWORD(l) ((uint16_t)(uint32_t)(l))
#endif

#ifndef HIWORD
#define HIWORD(l) ((uint16_t)((((uint32_t)(l)) >> 16) & 0xFFFF))
#endif

#ifndef LOBYTE
#define LOBYTE(l) ((uint8_t)(uint16_t)(l))
#endif

#ifndef HIBYTE
#define HIBYTE(l) ((uint8_t)((((uint16_t)(l)) >> 8) & 0xFF))
#endif

#ifdef _X86_16
/* For 16-bit addresses, we have to assume that the upper 32 bits
 * are zero. */
#ifndef LODWORD
#define LODWORD(l)                              (l)
#endif

#ifndef HIDWORD
#define HIDWORD(l)                              (0)
#endif
#else
#ifdef __OSCONFIG_DISABLE_64BIT
/* int is 32-bits, no 64-bit support on this platform */
#ifndef LODWORD
#define LODWORD(l)                              ((u32)(l))
#endif

#ifndef HIDWORD
#define HIDWORD(l)                              (0)
#endif
#else
/* Full 64-bit address/integer on both 32-bit and 64-bit platforms */
#ifndef LODWORD
#define LODWORD(l)                              ((uint32_t)(uint64_t)(l))
#endif

#ifndef HIDWORD
#define HIDWORD(l)                              (((*(Integer64_t*)(&(l)))).u.HighPart)
#endif
#endif
#endif

#endif //!__OS_DEFINITIONS__
