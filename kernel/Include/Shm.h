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
* MollenOS MCore - Shared Memory System
*/

#ifndef __MCORE_SHARED_MEMORY__
#define __MCORE_SHARED_MEMORY__

/* Includes */
#include <Arch.h>
#include <ProcessManager.h>
#include <Heap.h>
#include <stdint.h>
#include <crtdefs.h>

/* Definitions */


/* Structures */
typedef struct _MCoreShmBlock
{
	/* Address(es) */
	Addr_t KernelAddress;
	Addr_t ProcAddress;

	/* Length */
	size_t Length;

	/* Allocation Status */
	int Allocated;

	/* Link */
	struct _MCoreShmBlock *Link;

} MCoreShmBlock_t;

typedef struct _MCoreShmNode
{
	/* Process Id */
	PId_t ProcessId;

	/* Blocks */
	MCoreShmBlock_t *Blocks;

	/* Link */
	struct _MCoreShmNode *Link;

} MCoreShmNode_t;

typedef struct _MCoreShmManager
{
	/* The Heap */
	Heap_t *Heap;

	/* List of nodes */
	MCoreShmNode_t *ProcessNodes;

} MCoreShmManager_t;

/* Prototypes */
_CRT_EXTERN void ShmInit(void);

/* Allocate & Free Memory */
_CRT_EXTERN Addr_t ShmAllocateForProcess(PId_t ProcessId, 
	AddressSpace_t *AddrSpace, Addr_t Address, size_t Length);
_CRT_EXTERN Addr_t ShmFreeForProcess(PId_t ProcessId, 
	AddressSpace_t *AddrSpace, Addr_t Address);


#endif //!__MCORE_SHARED_MEMORY__