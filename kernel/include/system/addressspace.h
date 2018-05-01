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
 * - Contains the shared memory addressing space interface
 *   that all sub-layers / architectures must conform to
 */
#ifndef _MCORE_ADDRESSSINGSPACE_H_
#define _MCORE_ADDRESSSINGSPACE_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>

/* AddressSpace Definitions
 * Definitions, bit definitions and magic constants for address spaces */
#define ASPACE_DATASIZE                 4

/* AddressSpace (Type) Definitions
 * Definitions, bit definitions and magic constants for address spaces */
#define ASPACE_TYPE_KERNEL              0x00000001
#define ASPACE_TYPE_INHERIT             0x00000002
#define ASPACE_TYPE_APPLICATION         0x00000004
#define ASPACE_TYPE_DRIVER              0x00000008

/* AddressSpace (Flags) Definitions
 * Definitions, bit definitions and magic constants for address spaces */
#define ASPACE_FLAG_APPLICATION         0x00000001  // Userspace mapping
#define ASPACE_FLAG_NOCACHE             0x00000002  // Disable caching for mapping
#define ASPACE_FLAG_VIRTUAL             0x00000004  // Virtual mapping (Physical address is supplied)
#define ASPACE_FLAG_CONTIGIOUS          0x00000008  // Contigious physical pages
#define ASPACE_FLAG_SUPPLIEDVIRTUAL     0x00000010  // Virtual base-page is supplied
#define ASPACE_FLAG_READONLY            0x00000020  // Memory can only be read
#define ASPACE_FLAG_EXECUTABLE          0x00000040  // Memory can be executed

/* Address Space Structure
 * Denotes the must have and architecture specific
 * members of an addressing space */
PACKED_TYPESTRUCT(AddressSpace, {
    UUId_t                  Id;
    Spinlock_t              Lock;
    int                     References;
    Flags_t                 Flags;
    
    uintptr_t               Data[ASPACE_DATASIZE];
});

/* AddressSpaceInitialize
 * Initializes the Kernel Address Space. This only copies the data into a static global
 * storage, which means users should just pass something temporary structure */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceInitialize(
    _In_ AddressSpace_t *KernelSpace);

/* AddressSpaceCreate
 * Initialize a new address space, depending on what user is requesting we 
 * might recycle a already existing address space */
KERNELAPI
AddressSpace_t*
KERNELABI
AddressSpaceCreate(
    _In_ Flags_t Flags);

/* AddressSpaceDestroy
 * Destroy and release all resources related to an address space, 
 * only if there is no more references */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceDestroy(
    _In_ AddressSpace_t *AddressSpace);

/* AddressSpaceSwitch
 * Switches the current address space out with the the address space provided 
 * for the current cpu */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceSwitch(
    _In_ AddressSpace_t *AddressSpace);

/* AddressSpaceGetCurrent
 * Returns the current address space if there is no active threads or threading
 * is not setup it returns the kernel address space */
KERNELAPI
AddressSpace_t*
KERNELABI
AddressSpaceGetCurrent(void);

/* AddressSpaceChangeProtection
 * Changes the protection parameters for the given memory region.
 * The region must already be mapped and the size will be rounded up
 * to a multiple of the page-size. */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceChangeProtection(
    _In_        AddressSpace_t*     AddressSpace,
    _InOut_Opt_ VirtualAddress_t    VirtualAddress, 
    _In_        size_t              Size, 
    _In_        Flags_t             Flags,
    _Out_       Flags_t*            PreviousFlags);

/* AddressSpaceMap
 * Maps the given virtual address into the given address space
 * uses the given physical pages instead of automatic allocation
 * It returns the start address of the allocated physical region */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceMap(
    _In_        AddressSpace_t*     AddressSpace,
    _InOut_Opt_ PhysicalAddress_t*  PhysicalAddress, 
    _InOut_Opt_ VirtualAddress_t*   VirtualAddress, 
    _In_        size_t              Size, 
    _In_        Flags_t             Flags,
    _In_        uintptr_t           Mask);

/* AddressSpaceUnmap
 * Unmaps a virtual memory region from an address space */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceUnmap(
    _In_ AddressSpace_t*    AddressSpace, 
    _In_ VirtualAddress_t   Address, 
    _In_ size_t             Size);

/* AddressSpaceGetMapping
 * Retrieves a physical mapping from an address space determined
 * by the virtual address given */
KERNELAPI
PhysicalAddress_t
KERNELABI
AddressSpaceGetMapping(
    _In_ AddressSpace_t*    AddressSpace, 
    _In_ VirtualAddress_t   VirtualAddress);

/* AddressSpaceGetPageSize
 * Retrieves the memory page-size used by the underlying architecture. */
KERNELAPI
size_t
KERNELABI
AddressSpaceGetPageSize(void);

/* AddressSpaceIsDirty
 * Checks if the given virtual address is dirty (has been written data to). 
 * Returns OsSuccess if the address is dirty. */
KERNELAPI
OsStatus_t
KERNELABI
AddressSpaceIsDirty(
    _In_ AddressSpace_t*    AddressSpace,
    _In_ VirtualAddress_t   Address);

#endif //!_MCORE_ADDRESSSINGSPACE_H_
