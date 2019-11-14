/**
 * MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * OS Basic Definitions & Structures
 * - This header describes the base structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_DEFINITIONS__
#define __OS_DEFINITIONS__

#include <crtdefs.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

#if !defined(__cplusplus)
#if defined __STDC_VERSION__ && __STDC_VERSION__ >= 201112L
#include <stdatomic.h>
#include <stdnoreturn.h>
#include <stdbool.h>
#endif // __STDC_VERSION__ >= 201112L
#endif // !__cplusplus

#define DECL_STRUCT(Type) typedef struct Type Type##_t

#ifdef _UNICODE
# define TCHAR wchar_t
# define _T(x) L ##x
#else
# define TCHAR char
# define _T(x) x
#endif

/**
 * Memory / Addressing types below 
 * these will switch in size based upon target-arch 
 */
typedef uint32_t                    reg32_t;
typedef uint64_t                    reg64_t;

#if defined(i386) || defined(__i386__)
#define __BITS                      32
#define __MASK                      0xFFFFFFFF
typedef unsigned int                PhysicalAddress_t;
typedef unsigned int                VirtualAddress_t;
typedef reg32_t                     reg_t;
#elif defined(__x86_64__) || defined(amd64) || defined(__amd64__)
#define __BITS                      64
#define __MASK                      0xFFFFFFFFFFFFFFFF
typedef unsigned long long          PhysicalAddress_t;
typedef unsigned long long          VirtualAddress_t;
typedef reg64_t                     reg_t;
#endif

/**
 * Operation System Types 
 * these are usually fixed no matter arch and include stuff
 * as threading, processing etc 
 */
typedef unsigned int IntStatus_t;
typedef size_t       UUId_t; 
typedef unsigned int Flags_t;
typedef unsigned     DevInfo_t;
typedef void*        Handle_t;

#define UUID_INVALID                (UUId_t)-1

#define HANDLE_INVALID              (Handle_t)0
#define HANDLE_GLOBAL               (Handle_t)1

typedef enum {
    OsSuccess   = 0,
    OsError,             // Error - Generic
    OsExists,            // Error - Resource already exists
    OsDoesNotExist,      // Error - Resource does not exist
    OsInvalidParameters, // Error - Bad parameters given
    OsInvalidPermissions,// Error - Bad permissions
    OsTimeout,           // Error - Timeout
    OsNotSupported,      // Error - Feature not supported
    OsOutOfMemory,       // Error - Out of memory
    OsBusy,              // Error - Resource is busy or contended
    
    OsErrorCodeCount
} OsStatus_t;

typedef enum {
    InterruptNotHandled,
    InterruptHandled,       // Handled, notify process
    InterruptHandledStop,   // Handled, do not notify process
} InterruptStatus_t;

typedef union LargeInteger {
    struct {
        uint32_t LowPart;
        int32_t  HighPart;
    } s;
    struct {
        uint32_t LowPart;
        uint32_t HighPart;
    } u;
    int64_t QuadPart;
} LargeInteger_t;

typedef union LargeUInteger {
    struct {
        uint32_t LowPart;
        int32_t  HighPart;
    } s;
    struct {
        uint32_t LowPart;
        uint32_t HighPart;
    } u;
    uint64_t QuadPart;
} LargeUInteger_t;

static inline int
FirstSetBit(size_t Value)
{
    int bCount = 0;
    size_t Cc = Value;

    for (; Cc != 0;) {
        bCount++;
        Cc >>= 1;
    }
    return bCount;
}

static inline int
LastSetBit(size_t Value)
{
    size_t _Val = Value;
    int bIndex = 0;

    while (_Val >>= 1) {
        bIndex++;
    }
    return bIndex;
}

#define _MAXPATH            512
#define MIN(a,b)                                (((a)<(b))?(a):(b))
#define MAX(a,b)                                (((a)>(b))?(a):(b))
#define ISINRANGE(val, min, max)                (((val) >= (min)) && ((val) <= (max)))
#define DIVUP(a, b)                             ((a / b) + (((a % b) > 0) ? 1 : 0))
#define INCLIMIT(i, limit)                      i++; if (i == limit) i = 0;
#define ADDLIMIT(Base, Current, Step, Limit)    ((Current + Step) >= Limit) ? Base : (Current + Step) 
#define ALIGN(Val, Alignment, Roundup)          ((Val & (Alignment-1)) > 0 ? (Roundup == 1 ? ((Val + Alignment) & ~(Alignment-1)) : Val & ~(Alignment-1)) : Val)
#define ISALIGNED(Val, Alignment)               ((Val & (Alignment-1)) == 0)
#define BOCHSBREAK                              __asm__ __volatile__ ("xchg %bx, %bx\n\t");

#ifdef __COMPILE_ASSERT
#define STATIC_ASSERT(COND,MSG)                 typedef char static_assertion_##MSG[(!!(COND))*2-1]
#define COMPILE_TIME_ASSERT3(X,L)               STATIC_ASSERT(X,static_assertion_at_line_##L)
#define COMPILE_TIME_ASSERT2(X,L)               COMPILE_TIME_ASSERT3(X,L)
#define COMPILE_TIME_ASSERT(X)                  COMPILE_TIME_ASSERT2(X,__LINE__)
#endif

#define FSEC_PER_NSEC                           1000000L
#define NSEC_PER_MSEC                           1000L
#define MSEC_PER_SEC                            1000L
#define NSEC_PER_SEC                            1000000000L
#define FSEC_PER_SEC                            1000000000000000LL

#ifndef LOWORD
#define LOWORD(l)                               ((uint16_t)(uint32_t)(l))
#endif

#ifndef HIWORD
#define HIWORD(l)                               ((uint16_t)((((uint32_t)(l)) >> 16) & 0xFFFF))
#endif

#ifndef LOBYTE
#define LOBYTE(l)                               ((uint8_t)(uint16_t)(l))
#endif

#ifndef HIBYTE
#define HIBYTE(l)                               ((uint8_t)((((uint16_t)(l)) >> 8) & 0xFF))
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
#ifdef _MOLLENOS_NO_64BIT
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
#define HIDWORD(l)                              (((*(LargeInteger_t*)(&l))).u.HighPart)
#endif
#endif
#endif

#endif //!__OS_DEFINITIONS__
