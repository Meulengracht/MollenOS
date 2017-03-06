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
 * MollenOS Address Space Interface
 * - Contains the shared kernel addressing space interface
 *   that all sub-layers / architectures must conform to
 */


#ifndef _MCORE_ADDRESSINGSPACE_H_
#define _MCORE_ADDRESSINGSPACE_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>

/* Includes
 * - System */
#include <arch.h>

/* Address Space Structure
 * Denotes the must have and architecture specific
 * members of an addressing space */
PACKED_TYPESTRUCT(AddressSpace, {
	Spinlock_t				Lock;
	int						References;
	Flags_t					Flags;

	// Architecture Members
	ADDRESSSPACE_MEMBERS
});

/* Address space creation flags, use these
 * to specify which kind of address space that is
 * created */
#define AS_TYPE_KERNEL					0x00000001
#define AS_TYPE_INHERIT					0x00000002
#define AS_TYPE_APPLICATION				0x00000004
#define AS_TYPE_DRIVER					0x00000008

/* Address space mapping flags, use these to
 * specify which kind of address-map that is
 * being doine */
#define AS_FLAG_APPLICATION				0x00000001
#define AS_FLAG_RESERVE					0x00000002
#define AS_FLAG_NOCACHE					0x00000004
#define AS_FLAG_VIRTUAL					0x00000008
#define AS_FLAG_CONTIGIOUS				0x00000010

/* AddressSpaceInitKernel
 * Initializes the Kernel Address Space 
 * This only copies the data into a static global
 * storage, which means users should just pass something
 * temporary structure */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceInitKernel(
	_In_ AddressSpace_t *Kernel);

/* AddressSpaceCreate
 * Initialize a new address space, depending on 
 * what user is requesting we might recycle a already
 * existing address space */
KERNELAPI
AddressSpace_t*
KERNELABI
AddressSpaceCreate(
	_In_ Flags_t Flags);

/* AddressSpaceDestroy
 * Destroy and release all resources related
 * to an address space, only if there is no more
 * references */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceDestroy(
	_In_ AddressSpace_t *AddressSpace);

/* AddressSpaceSwitch
 * Switches the current address space out with the
 * the address space provided for the current cpu */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceSwitch(
	_In_ AddressSpace_t *AddressSpace);

/* AddressSpaceGetCurrent
 * Returns the current address space
 * if there is no active threads or threading
 * is not setup it returns the kernel address space */
KERNELAPI
AddressSpace_t*
KERNELABI
AddressSpaceGetCurrent(void);

/* AddressSpaceTranslate
 * Translates the given address to the correct virtual
 * address, this can be used to correct any special cases on
 * virtual addresses in the sub-layer */
KERNELAPI
VirtAddr_t
KERNELABI
AddressSpaceTranslate(
	_In_ AddressSpace_t *AddressSpace,
	_In_ VirtAddr_t Address);

/* AddressSpaceMap
 * Maps the given virtual address into the given address space
 * automatically allocates physical pages based on the passed Flags
 * It returns the start address of the allocated physical region */
KERNELAPI
PhysAddr_t
KERNELABI
AddressSpaceMap(
	_In_ AddressSpace_t *AddressSpace,
	_In_ VirtAddr_t Address, 
	_In_ size_t Size,
	_In_ Addr_t Mask, 
	_In_ Flags_t Flags);

/* AddressSpaceMapFixed
 * Maps the given virtual address into the given address space
 * uses the given physical pages instead of automatic allocation
 * It returns the start address of the allocated physical region */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceMapFixed(
	_In_ AddressSpace_t *AddressSpace,
	_In_ PhysAddr_t pAddress, 
	_In_ VirtAddr_t vAddress, 
	_In_ size_t Size, 
	_In_ Flags_t Flags);

/* AddressSpaceUnmap
 * Unmaps a virtual memory region from an address space */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceUnmap(
	_In_ AddressSpace_t *AddressSpace, 
	_In_ VirtAddr_t Address, 
	_In_ size_t Size);

/* AddressSpaceGetMap
 * Retrieves a physical mapping from an address space determined
 * by the virtual address given */
KERNELAPI
PhysAddr_t
KERNELABI
AddressSpaceGetMap(
	_In_ AddressSpace_t *AddressSpace, 
	_In_ VirtAddr_t Address);

#endif //!_MCORE_ADDRESSINGSPACE_H_
