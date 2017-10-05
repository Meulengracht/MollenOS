/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Basic Definitions & Structures
 * - This header describes the base structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _OS_DEFITIONS_H_
#define _OS_DEFITIONS_H_

/* Include the standard integer
 * definitions header, we need them */
#include <crtdefs.h>
#include <stdint.h>

#ifdef _UNICODE
# define TCHAR wchar_t
# define _T(x) L ##x
#else
# define TCHAR char
# define _T(x) x
#endif

/* Memory / Addressing types below 
 * these will switch in size based upon target-arch */
typedef uint32_t                    reg32_t;
typedef uint64_t                    reg64_t;

/* Variable Width */
#if defined(_X86_32) || defined(i386)
#define __BITS                      32
#define __MASK                      0xFFFFFFFF
typedef unsigned int                PhysicalAddress_t;
typedef unsigned int                VirtualAddress_t;
typedef reg32_t                     reg_t;
#elif defined(_X86_64)
#define __BITS                      64
#define __MASK                      0xFFFFFFFFFFFFFFFF
typedef unsigned long long          PhysicalAddress_t;
typedef unsigned long long          VirtualAddress_t;
typedef reg64_t                     reg_t;
#endif

/* Operation System types below 
 * these are usually fixed no matter arch and include stuff
 * as threading, processing etc */
typedef unsigned int                IntStatus_t;
typedef unsigned int                UUId_t; 
typedef unsigned int                Flags_t;
typedef unsigned                    DevInfo_t;
typedef void *                      Handle_t;

/* Define some special UUId_t constants 
 * Especially a constant for invalid */
#define UUID_INVALID                (UUId_t)-1

/* Define some special UUId_t constants 
 * Especially a constant for invalid */
#define HANDLE_INVALID              (Handle_t)0

/* This definies various possible results
 * from certain os-operations */
typedef enum {
    OsSuccess,
    OsError
} OsStatus_t;

typedef enum {
    InterruptHandled,
    InterruptNotHandled
} InterruptStatus_t;

/* Define the standard os
 * rectangle used for ui operations */
typedef struct _mRectangle {
    int x, y;
    int w, h;
} Rect_t;

/* 64 Bit Integer
 * The parts can be accessed in both unsigned and
 * signed methods, but the quad-part is signed */
typedef union _LargeInteger {
    struct {
        uint32_t        LowPart;
        int32_t         HighPart;
    } s;
    struct {
        uint32_t        LowPart;
        uint32_t        HighPart;
    } u;
    int64_t             QuadPart;
} LargeInteger_t;

/* Helper function, retrieves the first 
 * set bit in a set of bits */
static int FirstSetBit(size_t Value)
{
    // Variables
    int bCount = 0;
    size_t Cc = Value;

    // Keep bit-shifting
    for (; Cc != 0;) {
        bCount++;
        Cc >>= 1;
    }
    return bCount;
}

/* Helper function, retrieves the last 
 * set bit in a set of bits */
static int LastSetBit(size_t Value)
{
    // Variables
    size_t _Val = Value;
    int bIndex = 0;

    // Keep shifting untill we 
    // reach a zero value 
    while (_Val >>= 1) {
        bIndex++;
    }
    return bIndex;
}

/* The max-path we support in the OS
 * for file-paths, in MollenOS we support
 * rather long paths */
#define _MAXPATH            512

/* Utils Definitions */
#define MIN(a,b)                                (((a)<(b))?(a):(b))
#define MAX(a,b)                                (((a)>(b))?(a):(b))
#define DIVUP(a, b)                             ((a / b) + (((a % b) > 0) ? 1 : 0))
#define INCLIMIT(i, limit)                      i++; if (i == limit) i = 0;
#define ADDLIMIT(Base, Current, Step, Limit)    ((Current + Step) >= Limit) ? Base : (Current + Step) 
#define ALIGN(Val, Alignment, Roundup)          ((Val & (Alignment-1)) > 0 ? (Roundup == 1 ? ((Val + Alignment) & ~(Alignment-1)) : Val & ~(Alignment-1)) : Val)
#define ISALIGNED(Val, Alignment)               ((Val & (Alignment-1)) == 0)

/* Time definitions that can help with 
 * conversion of the different time-units */
#define FSEC_PER_NSEC                           1000000L
#define NSEC_PER_MSEC                           1000L
#define MSEC_PER_SEC                            1000L
#define NSEC_PER_SEC                            1000000000L
#define FSEC_PER_SEC                            1000000000000000LL

/* Data manipulation macros */
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

#endif //!_OS_DEFITIONS_H_
