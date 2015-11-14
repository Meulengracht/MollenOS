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
* MollenOS Module Shared Library ( Memory Functions )
*/

/* Includes */
#include <crtdefs.h>
#include <Module.h>

/* Typedefs */
typedef void *(*kMemAlloc)(size_t Size);
typedef void *(*__allocaligned)(size_t Size);
typedef void (*kMemFree)(void *Ptr);

typedef VirtAddr_t *(*__mapsysmem)(PhysAddr_t PhysicalAddr, int Pages);
typedef PhysAddr_t(*__allocdmapage)(void);
typedef void (*__freedmapage)(PhysAddr_t Addr);

typedef PhysAddr_t (*__getvirtmapping)(void *PageDirectory, VirtAddr_t VirtualAddr);

/* Heap Operations */
void *kmalloc(size_t sz)
{
	return ((kMemAlloc)GlbFunctionTable[kFuncMemAlloc])(sz);
}

void *kmalloc_a(size_t sz)
{
	return ((__allocaligned)GlbFunctionTable[kFuncMemAllocAligned])(sz);
}

void kfree(void *p)
{
	((kMemFree)GlbFunctionTable[kFuncMemAlloc])(p);
}

/* Map memory to system mem */
VirtAddr_t *MmVirtualMapSysMemory(PhysAddr_t PhysicalAddr, int Pages)
{
	return ((__mapsysmem)GlbFunctionTable[kFuncMemMapDeviceMem])(PhysicalAddr, Pages);
}

/* Allocate low-memory */
PhysAddr_t MmPhysicalAllocateBlockDma(void)
{
	return ((__allocdmapage)GlbFunctionTable[kFuncMemAllocDma])();
}

/* Get physical address that virt maps to */
PhysAddr_t MmVirtualGetMapping(void *PageDirectory, VirtAddr_t VirtualAddr)
{
	return ((__getvirtmapping)GlbFunctionTable[kFuncMemGetMapping])(PageDirectory, VirtualAddr);
}

/* Free a physical mem block */
void MmPhysicalFreeBlock(PhysAddr_t Addr)
{
	((__freedmapage)GlbFunctionTable[kFuncMemFreeDma])(Addr);
}