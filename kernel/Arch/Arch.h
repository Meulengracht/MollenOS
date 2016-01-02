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

/* Select Correct ARCH file */
#if defined(_X86_32)
#include "x86\x32\Arch.h"
#elif defined(_X86_64)
#include "x86\x64\Arch.h"
#else
#error "Unsupported Architecture :("
#endif

/* Includes */
#include <crtdefs.h>

/* Definitions */
typedef int OsStatus_t;

/* These must be implemented by 
 * the underlying architecture */

/***********************
* Address Spaces       *
* Used for abstracting *
* the virtual memory   *
***********************/
#define ADDRESS_SPACE_KERNEL		0x1
#define ADDRESS_SPACE_INHERIT		0x2
#define ADDRESS_SPACE_USER			0x4

_CRT_EXTERN AddressSpace_t *AddressSpaceCreate(uint32_t Flags);
_CRT_EXTERN void AddressSpaceDestroy(AddressSpace_t *AddrSpace);
_CRT_EXTERN void AddressSpaceSwitch(AddressSpace_t *AddrSpace);
_CRT_EXTERN AddressSpace_t *AddressSpaceGetCurrent(void);

_CRT_EXTERN void AddressSpaceReleaseKernel(AddressSpace_t *AddrSpace);
_CRT_EXTERN void AddressSpaceMap(AddressSpace_t *AddrSpace, 
	VirtAddr_t Address, size_t Size, int UserMode);
_CRT_EXTERN void AddressSpaceMapFixed(AddressSpace_t *AddrSpace,
	PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr, size_t Size, int UserMode);
_CRT_EXTERN void AddressSpaceUnmap(AddressSpace_t *AddrSpace, VirtAddr_t Address, size_t Size);
_CRT_EXTERN PhysAddr_t AddressSpaceGetMap(AddressSpace_t *AddrSpace, VirtAddr_t Address);

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
	int Id;

	/* Type */
	int Type;

	/* Base */
	Addr_t PhysicalBase;
	Addr_t VirtualBase;

	/* Size */
	size_t Size;

} DeviceIoSpace_t;

/* Functions */
_CRT_EXTERN void IoSpaceInit(void);
_CRT_EXTERN DeviceIoSpace_t *IoSpaceCreate(int Type, Addr_t PhysicalBase, size_t Size);
_CRT_EXTERN void IoSpaceDestroy(DeviceIoSpace_t *IoSpace);

_CRT_EXTERN size_t IoSpaceRead(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Length);
_CRT_EXTERN void IoSpaceWrite(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Value, size_t Length);
_CRT_EXTERN Addr_t IoSpaceValidate(Addr_t Address);

/***********************
* Spinlock Interface   *
***********************/
_CRT_EXTERN void SpinlockReset(Spinlock_t *Spinlock);
_CRT_EXPORT OsStatus_t SpinlockAcquire(Spinlock_t *Spinlock);
_CRT_EXPORT void SpinlockRelease(Spinlock_t *Spinlock);

/***********************
* Device Interface     *
***********************/
_CRT_EXTERN void DevicesInit(void *Args);

#endif