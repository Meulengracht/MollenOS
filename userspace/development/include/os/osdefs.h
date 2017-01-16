/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - Operating System Types 
*/

#ifndef _OS_DEFITIONS_H_
#define _OS_DEFITIONS_H_

/* Include the standard integer
 * definitions header, we need them */
#include <crtdefs.h>
#include <stdint.h>

/* Memory / Addressing types below 
 * these will switch in size based upon target-arch */

/* Fixed Width */
typedef uint32_t reg32_t;
typedef uint64_t reg64_t;

/* Variable Width */
#if defined(_X86_32)
typedef unsigned int PhysAddr_t;
typedef unsigned int VirtAddr_t;
typedef unsigned int Addr_t;
typedef int SAddr_t;
typedef unsigned int Cpu_t;
typedef reg32_t reg_t;
#elif defined(_X86_64)
typedef unsigned long long PhysAddr_t;
typedef unsigned long long VirtAddr_t;
typedef unsigned long long Addr_t;
typedef long long SAddr_t;
typedef unsigned long long Cpu_t;
typedef reg64_t reg_t;
#endif

/* Operation System types below 
 * these are usually fixed no matter arch and include stuff
 * as threading, processing etc */
typedef unsigned int UUId_t;
typedef unsigned int Flags_t;
typedef unsigned DevInfo_t;

/* Define some special UUId_t constants 
 * Especially a constant for invalid */
#define UUID_INVALID			(unsigned int)-1

/* This definies various possible results
 * from certain os-operations */
typedef enum {
	OsNoError,
	OsError
} OsStatus_t;

/* Define the standard os
 * rectangle used for ui
 * operations */
#ifndef MRECTANGLE_DEFINED
#define MRECTANGLE_DEFINED
typedef struct _mRectangle {
	int x, y;
	int w, h;
} Rect_t;
#endif

/* Helper function, retrieves the first 
 * set bit in a set of bits */
static int FirstSetBit(size_t Value)
{
	/* Vars */
	int bCount = 0;
	size_t Cc = Value;

	/* Keep bit-shifting */
	for (; Cc != 0;) {
		bCount++;
		Cc >>= 1;
	}

	/* Done */
	return bCount;
}

/* Helper function, retrieves the last 
 * set bit in a set of bits */
static int LastSetBit(size_t Value)
{
	/* Variables */
	size_t _Val = Value;
	int bIndex = 0;

	/* Keep shifting untill we 
	 * reach a zero value */
	while (_Val >>= 1) {
		bIndex++;
	}

	/* Done! */
	return bIndex;
}

/* Utils Definitions */
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define DIVUP(a, b) ((a / b) + (((a % b) > 0) ? 1 : 0))
#define INCLIMIT(i, limit) i++; if (i == limit) i = 0;
#define ALIGN(Val, Alignment, Roundup) ((Val & (Alignment-1)) > 0 ? (Roundup == 1 ? ((Val + Alignment) & ~(Alignment-1)) : Val & ~(Alignment-1)) : Val)
#define ISALIGNED(Val, Alignment)	((Val & (Alignment-1)) == 0)

/* Data manipulation macros */
#ifndef LOWORD
#define LOWORD(l)                       ((uint16_t)(uint32_t)(l))
#endif

#ifndef HIWORD
#define HIWORD(l)                       ((uint16_t)((((uint32_t)(l)) >> 16) & 0xFFFF))
#endif

#ifndef LOBYTE
#define LOBYTE(l)                       ((uint8_t)(uint16_t)(l))
#endif

#ifndef HIBYTE
#define HIBYTE(l)                       ((uint8_t)((((uint16_t)(l)) >> 8) & 0xFF))
#endif

#ifdef _X86_16
/* For 16-bit addresses, we have to assume that the upper 32 bits
 * are zero. */
#ifndef LODWORD
#define LODWORD(l)                      (l)
#endif

#ifndef HIDWORD
#define HIDWORD(l)                      (0)
#endif
#else
#ifdef _MOLLENOS_NO_64BIT
/* int is 32-bits, no 64-bit support on this platform */
#ifndef LODWORD
#define LODWORD(l)                      ((u32)(l))
#endif

#ifndef HIDWORD
#define HIDWORD(l)                      (0)
#endif
#else
/* Full 64-bit address/integer on both 32-bit and 64-bit platforms */
#ifndef LODWORD
#define LODWORD(l)                      ((uint32_t)(uint64_t)(l))
#endif

#ifndef HIDWORD
#define HIDWORD(l)                      ((uint32_t)(((*(val64_t*)(&l))).HighPart))
#endif
#endif
#endif

#endif //!_OS_DEFITIONS_H_
