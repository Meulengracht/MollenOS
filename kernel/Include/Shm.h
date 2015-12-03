/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
#include <stdint.h>
#include <crtdefs.h>

/* Definitions */


/* Structures */
typedef struct _MCoreShmBlock
{
	/* Address */
	Addr_t Address;

	/* Length */
	size_t Length;

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
	/* List of nodes */
	MCoreShmNode_t *Nodes;

} MCoreShmManager_t;

/* Prototypes */
_CRT_EXTERN void ShmInit(void);

/* Allocate Memory */
_CRT_EXTERN void ShmAllocateForProcess(PId_t ProcessId, 
	AddressSpace_t *AddrSpace, Addr_t Address, size_t Length);
_CRT_EXTERN void ShmFreeForProcess(PId_t ProcessId, 
	AddressSpace_t *AddrSpace, Addr_t Address);


#endif //!__MCORE_SHARED_MEMORY__