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
#if defined(_X86_32)
typedef unsigned int PhysAddr_t;
typedef unsigned int VirtAddr_t;
typedef unsigned int Addr_t;
typedef int SAddr_t;
typedef unsigned int Cpu_t;
#elif defined(_X86_64)
typedef unsigned long long PhysAddr_t;
typedef unsigned long long VirtAddr_t;
typedef unsigned long long Addr_t;
typedef long long SAddr_t;
typedef unsigned long long Cpu_t;
#endif

/* Operation System types below 
 * these are usually fixed no matter arch and include stuff
 * as threading, processing etc */
typedef unsigned int ProcId_t;
typedef unsigned int ThreadId_t;
typedef unsigned int TimerId_t;
typedef unsigned DevInfo_t;
typedef int DevId_t;

/* This definies various possible results
 * from certain os-operations */
typedef enum
{
	OsNoError,
	OsError

} OsStatus_t;

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

/* Utils Definitions */
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define DIVUP(a, b) ((a / b) + (((a % b) > 0) ? 1 : 0))
#define INCLIMIT(i, limit) i++; if (i == limit) i = 0;
#define ALIGN(Val, Alignment, Roundup) ((Val & (Alignment-1)) > 0 ? (Roundup == 1 ? ((Val + Alignment) & ~(Alignment-1)) : Val & ~(Alignment-1)) : Val)

#endif //!_OS_DEFITIONS_H_
