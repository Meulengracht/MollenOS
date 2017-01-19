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

/* Address space creation flags, use these
 * to specify which kind of address space that is
 * created */
#define ADDRESS_SPACE_KERNEL			0x1
#define ADDRESS_SPACE_INHERIT			0x2
#define ADDRESS_SPACE_APPLICATION		0x4
#define ADDRESS_SPACE_DRIVER			0x8

/* Address space mapping flags, use these to
 * specify which kind of address-map that is
 * being doine */
#define ADDRESS_SPACE_FLAG_APPLICATION	0x1
#define ADDRESS_SPACE_FLAG_RESERVE		0x2
#define ADDRESS_SPACE_FLAG_NOCACHE		0x4
#define ADDRESS_SPACE_FLAG_VIRTUAL		0x8

/* AddressSpaceInitKernel
 * Initializes the Kernel Address Space 
 * This only copies the data into a static global
 * storage, which means users should just pass something
 * temporary structure */
__CRT_EXTERN void AddressSpaceInitKernel(AddressSpace_t *Kernel);

/* AddressSpaceCreate
 * Initialize a new address space, depending on 
 * what user is requesting we might recycle a already
 * existing address space */
__CRT_EXTERN AddressSpace_t *AddressSpaceCreate(Flags_t Flags);

/* AddressSpaceDestroy
 * Destroy and release all resources related
 * to an address space, only if there is no more
 * references */
__CRT_EXTERN void AddressSpaceDestroy(AddressSpace_t *AddrSpace);

/* AddressSpaceSwitch
 * Switches the current address space out with the
 * the address space provided for the current cpu */
__CRT_EXTERN void AddressSpaceSwitch(AddressSpace_t *AddrSpace);

/* AddressSpaceGetCurrent
 * Returns the current address space
 * if there is no active threads or threading
 * is not setup it returns the kernel address space */
__CRT_EXTERN AddressSpace_t *AddressSpaceGetCurrent(void);

/* AddressSpaceTranslate
 * Translates the given address to the correct virtual
 * address, this can be used to correct any special cases on
 * virtual addresses in the sub-layer */
__CRT_EXTERN Addr_t AddressSpaceTranslate(AddressSpace_t *AddrSpace, 
	Addr_t VirtualAddress);

_CRT_EXPORT Addr_t AddressSpaceMap(AddressSpace_t *AddrSpace, 
	VirtAddr_t Address, size_t Size, Addr_t Mask, int Flags);
_CRT_EXPORT void AddressSpaceMapFixed(AddressSpace_t *AddrSpace,
	PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr, size_t Size, int Flags);
_CRT_EXPORT void AddressSpaceUnmap(AddressSpace_t *AddrSpace, VirtAddr_t Address, size_t Size);

/* AddressSpaceGetMap
 * Retrieves a physical mapping from an address space 
 * for x86 we can simply just redirect it to MmVirtual */
__CRT_EXTERN PhysAddr_t AddressSpaceGetMap(AddressSpace_t *AddrSpace, VirtAddr_t Address);

/************************
 * Threading            *
 * Used for abstracting *
 * arch specific thread *
 ************************/
typedef struct _MCoreThread MCoreThread_t;

/* IThreadCreate
 * Initializes a new x86-specific thread context
 * for the given threading flags, also initializes
 * the yield interrupt handler first time its called */
__CRT_EXTERN void *IThreadCreate(Flags_t ThreadFlags, Addr_t EntryPoint);

/* IThreadSetupUserMode
 * Initializes user-mode data for the given thread, and
 * allocates all neccessary resources (x86 specific) for
 * usermode operations */
__CRT_EXTERN void IThreadSetupUserMode(MCoreThread_t *Thread, Addr_t StackAddress);

/* IThreadDestroy
 * Free's all the allocated resources for x86
 * specific threading operations */
__CRT_EXTERN void IThreadDestroy(MCoreThread_t *Thread);

/* IThreadWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
__CRT_EXTERN void IThreadWakeCpu(Cpu_t Cpu);

/* IThreadYield
 * Yields the current thread control to the scheduler */
__CRT_EXTERN void IThreadYield(void);

/************************
 * Device Io Spaces     *
 * Used for abstracting *
 * device addressing    *
 ************************/
#include <os/driver/io.h>

/* Represents an io-space in MollenOS, they represent
 * some kind of communication between hardware and software
 * by either port or mmio */
typedef struct _MCoreIoSpace {
	UUId_t				Id;
	UUId_t				Owner;
	int					Type;
	Addr_t				PhysicalBase;
	Addr_t				VirtualBase;
	size_t				Size;
} MCoreIoSpace_t;

/* Initialize the Io Space manager so we 
 * can register io-spaces from drivers and the
 * bus code */
__CRT_EXTERN void IoSpaceInitialize(void);

/* Registers an io-space with the io space manager 
 * and assigns the io-space a unique id for later
 * identification */
__CRT_EXTERN OsStatus_t IoSpaceRegister(DeviceIoSpace_t *IoSpace);

/* Acquires the given memory space by mapping it in
 * the current drivers memory space if needed, and sets
 * a lock on the io-space */
__CRT_EXTERN OsStatus_t IoSpaceAcquire(DeviceIoSpace_t *IoSpace);

/* Releases the given memory space by unmapping it from
 * the current drivers memory space if needed, and releases
 * the lock on the io-space */
__CRT_EXTERN OsStatus_t IoSpaceRelease(DeviceIoSpace_t *IoSpace);

/* Destroys the given io-space by its id, the id
 * has the be valid, and the target io-space HAS to 
 * un-acquired by any process, otherwise its not possible */
__CRT_EXTERN OsStatus_t IoSpaceDestroy(UUId_t IoSpace);

/* Tries to validate the given virtual address by 
 * checking if any process has an active io-space
 * that involves that virtual address */
__CRT_EXTERN Addr_t IoSpaceValidate(Addr_t Address);

/***********************
* Device Interface     *
***********************/
__CRT_EXTERN int DeviceAllocateInterrupt(void *mCoreDevice);

#endif