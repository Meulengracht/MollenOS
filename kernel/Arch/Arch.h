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
* MollenOS Architecture Header
*/

#ifndef _MCORE_MAIN_ARCH_
#define _MCORE_MAIN_ARCH_

/* Includes */
#include <os/osdefs.h>

/* The definition of a thread id
* used for identifying threads */
typedef void(*ThreadEntry_t)(void*);

/* Select Correct ARCH file */
#if defined(_X86_32)
#include "x86\x32\Arch.h"
#elif defined(_X86_64)
#include "x86\x64\Arch.h"
#else
#error "Unsupported Architecture :("
#endif

/* Typedef this */
typedef Registers_t Context_t;

/* These must be implemented by 
 * the underlying architecture */

/***********************
* Address Spaces       *
* Used for abstracting *
* the virtual memory   *
***********************/

/* This is the how many bits per register
* definition, used by the memory bitmap */
#if defined(_X86_32)
#define MEMORY_BITS					32
#define MEMORY_LIMIT				0xFFFFFFFF
#define MEMORY_MASK_DEFAULT			0xFFFFFFFF
#elif defined(_X86_64)
#define MEMORY_BITS					64
#define MEMORY_LIMIT				0xFFFFFFFFFFFFFFFF
#define MEMORY_MASK_DEFAULT			0xFFFFFFFFFFFFFFFF
#else
#error "Unsupported Architecture :("
#endif

/* Address Space Flags */
#define ADDRESS_SPACE_KERNEL		0x1
#define ADDRESS_SPACE_INHERIT		0x2
#define ADDRESS_SPACE_USER			0x4

/* Allocation Flags */
#define ADDRESS_SPACE_FLAG_USER			0x1
#define ADDRESS_SPACE_FLAG_RESERVE		0x2
#define ADDRESS_SPACE_FLAG_NOCACHE		0x4
#define ADDRESS_SPACE_FLAG_VIRTUAL		0x8

__CRT_EXTERN void AddressSpaceInitKernel(AddressSpace_t *Kernel);
__CRT_EXTERN AddressSpace_t *AddressSpaceCreate(int Flags);
__CRT_EXTERN void AddressSpaceDestroy(AddressSpace_t *AddrSpace);
__CRT_EXTERN void AddressSpaceSwitch(AddressSpace_t *AddrSpace);
_CRT_EXPORT AddressSpace_t *AddressSpaceGetCurrent(void);

__CRT_EXTERN void AddressSpaceReleaseKernel(AddressSpace_t *AddrSpace);
_CRT_EXPORT Addr_t AddressSpaceMap(AddressSpace_t *AddrSpace, 
	VirtAddr_t Address, size_t Size, Addr_t Mask, int Flags);
_CRT_EXPORT void AddressSpaceMapFixed(AddressSpace_t *AddrSpace,
	PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr, size_t Size, int Flags);
_CRT_EXPORT void AddressSpaceUnmap(AddressSpace_t *AddrSpace, VirtAddr_t Address, size_t Size);
_CRT_EXPORT PhysAddr_t AddressSpaceGetMap(AddressSpace_t *AddrSpace, VirtAddr_t Address);

/***********************
* Threading            *
* Used for abstracting *
* arch specific thread *
***********************/

/* Functions */
__CRT_EXTERN void *IThreadInitBoot(void);
__CRT_EXTERN void *IThreadInitAp(void);

__CRT_EXTERN void *IThreadInit(Addr_t EntryPoint);
__CRT_EXTERN void IThreadDestroy(void *ThreadData);

__CRT_EXTERN void IThreadInitUserMode(void *ThreadData,
	Addr_t StackAddr, Addr_t EntryPoint, Addr_t ArgumentAddress);
__CRT_EXTERN void IThreadWakeCpu(Cpu_t Cpu);
_CRT_EXPORT void IThreadYield(void);

/***********************
* Device Io Spaces     *
* Used for abstracting *
* device addressing    *
***********************/
#define DEVICE_IO_SPACE_IO		0x1
#define DEVICE_IO_SPACE_MMIO	0x2

/* Structures */
typedef struct _DeviceIoSpace
{
	/* Id */
	IoSpaceId_t Id;

	/* Type */
	int Type;

	/* Base */
	Addr_t PhysicalBase;
	Addr_t VirtualBase;

	/* Size */
	size_t Size;

} DeviceIoSpace_t;

/* Functions */
__CRT_EXTERN void IoSpaceInit(void);
_CRT_EXPORT DeviceIoSpace_t *IoSpaceCreate(int Type, Addr_t PhysicalBase, size_t Size);
_CRT_EXPORT void IoSpaceDestroy(DeviceIoSpace_t *IoSpace);

_CRT_EXPORT size_t IoSpaceRead(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Length);
_CRT_EXPORT void IoSpaceWrite(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Value, size_t Length);
__CRT_EXTERN Addr_t IoSpaceValidate(Addr_t Address);

/***********************
* Device Interface     *
***********************/
/* First of all, devices exists on TWO different
 * busses. PCI and PCI Express. */
__CRT_EXTERN void BusInit(void);

__CRT_EXTERN int DeviceAllocateInterrupt(void *mCoreDevice);

#endif