/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS Memory Space Interface
 * - Implementation of virtual memory address spaces. This underlying
 *   hardware must support the __OSCONFIG_HAS_MMIO defibne to use this.
 */

#ifndef __MEMORY_SPACE_INTERFACE__
#define __MEMORY_SPACE_INTERFACE__

#include <os/osdefs.h>
#include <ds/collection.h>

typedef struct _BlockBitmap BlockBitmap_t;

/* SystemMemorySpace Definitions
 * Definitions, bit definitions and magic constants for memory spaces */
#define MEMORY_DATACOUNT                4

/* SystemMemorySpace (Type) Definitions
 * Definitions, bit definitions and magic constants for memory spaces */
#define MEMORY_SPACE_INHERIT            0x00000001
#define MEMORY_SPACE_APPLICATION        0x00000002

/* SystemMemorySpace (Flags) Definitions
 * Definitions, bit definitions and magic constants for memory spaces */
#define MAPPING_USERSPACE               0x00000001  // Userspace mapping
#define MAPPING_NOCACHE                 0x00000002  // Disable caching for mapping
#define MAPPING_READONLY                0x00000004  // Memory can only be read
#define MAPPING_EXECUTABLE              0x00000008  // Memory can be executed
#define MAPPING_ISDIRTY                 0x00000010  // Memory that has been marked poluted/written to
#define MAPPING_PERSISTENT              0x00000020  // Memory should not be freed when mapping is removed
#define MAPPING_DOMAIN                  0x00000040  // Memory allocated for mapping must be domain local
#define MAPPING_COMMIT                  0x00000080  // Memory should be comitted immediately
#define MAPPING_LOWFIRST                0x00000100  // Memory resources should be allocated by low-addresses first

#define MAPPING_PHYSICAL_DEFAULT        0x00000001  // (Physical) Mappings are default allocated
#define MAPPING_PHYSICAL_CONTIGIOUS     0x00000002  // (Physical) Mappings are default allocated, as contigious
#define MAPPING_PHYSICAL_FIXED          0x00000004  // (Physical) Mappings are supplied
#define MAPPING_PHYSICAL_MASK           0x00000007

#define MAPPING_VIRTUAL_GLOBAL          0x00000008  // (Virtual) Mapping is done in global access memory
#define MAPPING_VIRTUAL_PROCESS         0x00000010  // (Virtual) Mapping is process specific
#define MAPPING_VIRTUAL_FIXED           0x00000020  // (Virtual) Mapping is supplied
#define MAPPING_VIRTUAL_MASK            0x00000038

typedef struct _SystemMemoryMappingHandler {
    CollectionItem_t Header;
    UUId_t           Handle;
    uintptr_t        Address;
    size_t           Length;
} SystemMemoryMappingHandler_t;

typedef struct _SystemMemorySpaceContext {
    Collection_t*  MemoryHandlers;
    BlockBitmap_t* HeapSpace;
    uintptr_t      SignalHandler;
} SystemMemorySpaceContext_t;

typedef struct _SystemMemorySpace {
    UUId_t                      ParentHandle;
    Flags_t                     Flags;
    uintptr_t                   Data[MEMORY_DATACOUNT];
    SystemMemorySpaceContext_t* Context;
} SystemMemorySpace_t;

/* InitializeMemorySpace
 * Initializes the system memory space. This initializes a static version of the
 * system memory space which is the default space the cpu should use for kernel operation. */
KERNELAPI OsStatus_t KERNELABI
InitializeMemorySpace(
    _In_ SystemMemorySpace_t* SystemMemorySpace);

/* CreateMemorySpace
 * Initialize a new memory space, depending on what user is requesting we 
 * might recycle a already existing address space */
KERNELAPI OsStatus_t KERNELABI
CreateMemorySpace(
    _In_  Flags_t Flags,
    _Out_ UUId_t* Handle);

/* DestroyMemorySpace
 * Callback invoked by the handle system when references on a process reaches zero */
KERNELAPI OsStatus_t KERNELABI
DestroyMemorySpace(
    _In_ void* Resource);

/* SwitchMemorySpace
 * Switches the current address space out with the the address space provided 
 * for the current cpu */
KERNELAPI OsStatus_t KERNELABI
SwitchMemorySpace(
    _In_ SystemMemorySpace_t* SystemMemorySpace);

/* GetCurrentMemorySpace
 * Returns the current address space if there is no active threads or threading
 * is not setup it returns the kernel address space */
KERNELAPI SystemMemorySpace_t* KERNELABI
GetCurrentMemorySpace(void);

/* GetDomainMemorySpace
 * Retrieves the system's current copy of its memory space. If domains are active it will
 * be for the current domain, if system is uma-mode it's the machine wide. */
KERNELAPI SystemMemorySpace_t* KERNELABI
GetDomainMemorySpace(void);

/* GetCurrentMemorySpaceHandle
 * Returns the current address space if there is no active threads or threading
 * is not setup it returns the kernel address space */
KERNELAPI UUId_t KERNELABI
GetCurrentMemorySpaceHandle(void);

/* AreMemorySpacesRelated 
 * Checks if two memory spaces are related to each other by sharing resources. */
KERNELAPI OsStatus_t KERNELABI
AreMemorySpacesRelated(
    _In_ SystemMemorySpace_t* Space1,
    _In_ SystemMemorySpace_t* Space2);

/* ChangeMemorySpaceProtection
 * Changes the protection parameters for the given memory region.
 * The region must already be mapped and the size will be rounded up
 * to a multiple of the page-size. */
KERNELAPI OsStatus_t KERNELABI
ChangeMemorySpaceProtection(
    _In_        SystemMemorySpace_t* SystemMemorySpace,
    _InOut_Opt_ VirtualAddress_t     VirtualAddress, 
    _In_        size_t               Size, 
    _In_        Flags_t              Flags,
    _Out_       Flags_t*             PreviousFlags);

/* CreateMemorySpaceMapping
 * Maps the given virtual address into the given address space
 * uses the given physical pages instead of automatic allocation
 * It returns the start address of the allocated physical region */
KERNELAPI OsStatus_t KERNELABI
CreateMemorySpaceMapping(
    _In_        SystemMemorySpace_t* SystemMemorySpace,
    _InOut_Opt_ PhysicalAddress_t*   PhysicalAddress, 
    _InOut_Opt_ VirtualAddress_t*    VirtualAddress,
    _In_        size_t               Size,
    _In_        Flags_t              MemoryFlags,
    _In_        Flags_t              PlacementFlags,
    _In_        uintptr_t            PhysicalMask);

/* CommitMemorySpaceMapping
 * Commits/finishes an already present memory mapping. If a physical address
 * is not already provided one will be allocated for the mapping. Flags must present. */
KERNELAPI OsStatus_t KERNELABI
CommitMemorySpaceMapping(
    _In_        SystemMemorySpace_t* SystemMemorySpace,
    _InOut_Opt_ PhysicalAddress_t*   PhysicalAddress, 
    _In_        VirtualAddress_t     VirtualAddress,
    _In_        uintptr_t            PhysicalMask);

/* CloneMemorySpaceMapping
 * Clones a region of memory mappings into the address space provided. The new mapping
 * will automatically be marked PERSISTANT and PROVIDED. */
KERNELAPI OsStatus_t KERNELABI
CloneMemorySpaceMapping(
    _In_        SystemMemorySpace_t* SourceSpace,
    _In_        SystemMemorySpace_t* DestinationSpace,
    _In_        VirtualAddress_t     SourceAddress,
    _InOut_Opt_ VirtualAddress_t*    DestinationAddress,
    _In_        size_t               Size,
    _In_        Flags_t              MemoryFlags,
    _In_        Flags_t              PlacementFlags);

/* RemoveMemorySpaceMapping
 * Unmaps a virtual memory region from an address space */
KERNELAPI OsStatus_t KERNELABI
RemoveMemorySpaceMapping(
    _In_ SystemMemorySpace_t* SystemMemorySpace, 
    _In_ VirtualAddress_t     Address, 
    _In_ size_t               Size);

/* GetMemorySpaceMapping
 * Retrieves a physical mapping from an address space determined
 * by the virtual address given */
KERNELAPI PhysicalAddress_t KERNELABI
GetMemorySpaceMapping(
    _In_ SystemMemorySpace_t*   SystemMemorySpace, 
    _In_ VirtualAddress_t       VirtualAddress);

/* GetMemorySpaceAttributes
 * Reads the attributes for a specific virtual memory address in the given space. */
KERNELAPI Flags_t KERNELABI
GetMemorySpaceAttributes(
    _In_ SystemMemorySpace_t* SystemMemorySpace, 
    _In_ VirtualAddress_t     VirtualAddress);

/* IsMemorySpacePageDirty
 * Checks if the given virtual address is dirty (has been written data to). 
 * Returns OsSuccess if the address is dirty. */
KERNELAPI OsStatus_t KERNELABI
IsMemorySpacePageDirty(
    _In_ SystemMemorySpace_t*   SystemMemorySpace,
    _In_ VirtualAddress_t       Address);

/* IsMemorySpacePagePresent
 * Checks if the given virtual address is present. Returns success if the page
 * at the address has a mapping. */
KERNELAPI OsStatus_t KERNELABI
IsMemorySpacePagePresent(
    _In_ SystemMemorySpace_t*   SystemMemorySpace,
    _In_ VirtualAddress_t       Address);

/* GetMemorySpacePageSize
 * Retrieves the memory page-size used by the underlying architecture. */
KERNELAPI size_t KERNELABI
GetMemorySpacePageSize(void);

#endif //!__MEMORY_SPACE_INTERFACE__
