/**
 * MollenOS
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
 * Memory Space Interface
 * - Implementation of virtual memory address spaces. This underlying
 *   hardware must support the __OSCONFIG_HAS_MMIO define to use this.
 */

#ifndef __MEMORY_SPACE_INTERFACE__
#define __MEMORY_SPACE_INTERFACE__

#include <os/osdefs.h>
#include <ds/list.h>
#include <mutex.h>
#include <utils/dynamic_memory_pool.h>

/* MemorySpace Definitions
 * Definitions, bit definitions and magic constants for memory spaces */
#define MEMORY_DATACOUNT 4

/* MemorySpace (Type) Definitions
 * Definitions, bit definitions and magic constants for memory spaces */
#define MEMORY_SPACE_INHERIT            0x00000001U
#define MEMORY_SPACE_APPLICATION        0x00000002U

/**
 * MemorySpace (Flags) Definitions
 * Definitions, bit definitions and magic constants for memory spaces
 * Default settings for mappings are READ|WRITE is set.
 */
#define MAPPING_USERSPACE               0x00000001U  // Userspace mapping
#define MAPPING_NOCACHE                 0x00000002U  // Disable caching for mapping
#define MAPPING_READONLY                0x00000004U  // Memory can only be read
#define MAPPING_EXECUTABLE              0x00000008U  // Memory can be executed
#define MAPPING_ISDIRTY                 0x00000010U  // Memory that has been marked poluted/written to
#define MAPPING_PERSISTENT              0x00000020U  // Memory should not be freed when mapping is removed
#define MAPPING_DOMAIN                  0x00000040U  // Memory allocated for mapping must be domain local
#define MAPPING_COMMIT                  0x00000080U  // Memory should be comitted immediately
#define MAPPING_LOWFIRST                0x00000100U  // Memory resources should be allocated by low-addresses first
#define MAPPING_GUARDPAGE               0x00000200U  // Memory resource is a stack and needs a guard page

#define MAPPING_PHYSICAL_FIXED          0x00000001U  // (Physical) Mappings are supplied

#define MAPPING_VIRTUAL_GLOBAL          0x00000002U  // (Virtual) Mapping is done in global access memory
#define MAPPING_VIRTUAL_PROCESS         0x00000004U  // (Virtual) Mapping is process specific
#define MAPPING_VIRTUAL_THREAD          0x00000008U  // (Virtual) Mapping is thread specific
#define MAPPING_VIRTUAL_FIXED           0x00000010U  // (Virtual) Mapping is supplied
#define MAPPING_VIRTUAL_MASK            0x0000001EU

#define MEMORYSPACE_GET(handle) (MemorySpace_t*)LookupHandleOfType(handle, HandleTypeMemorySpace);

typedef struct MemoryMappingHandler {
    element_t Header;
    UUId_t    Handle;
    uintptr_t Address;
    size_t    Length;
} MemoryMappingHandler_t;

// one per thread group [process]
typedef struct MemorySpaceContext {
    DynamicMemoryPool_t Heap;
    list_t*             MemoryHandlers;
    uintptr_t           SignalHandler;
} MemorySpaceContext_t;

// one per thread
typedef struct MemorySpace {
    UUId_t                ParentHandle;
    unsigned int          Flags;
    uintptr_t             Data[MEMORY_DATACOUNT];
    MemorySpaceContext_t* Context;
    DynamicMemoryPool_t   ThreadMemory;
} MemorySpace_t;

/**
 * InitializeMemorySpace
 * Initializes the system memory space. This initializes a static version of the
 * system memory space which is the default space the cpu should use for kernel operation.
 */
KERNELAPI OsStatus_t KERNELABI
InitializeMemorySpace(
        _In_ MemorySpace_t* SystemMemorySpace);

/**
 * CreateMemorySpace
 * Initialize a new memory space, depending on what user is requesting we 
 * might recycle a already existing address space
 */
KERNELAPI OsStatus_t KERNELABI
CreateMemorySpace(
    _In_  unsigned int Flags,
    _Out_ UUId_t* Handle);

/**
 * SwitchMemorySpace
 * Switches the current address space out with the the address space provided 
 * for the current cpu.
 */
KERNELAPI void KERNELABI
SwitchMemorySpace(
        _In_ MemorySpace_t*);

KERNELAPI UUId_t KERNELABI         GetCurrentMemorySpaceHandle(void);
KERNELAPI MemorySpace_t* KERNELABI GetCurrentMemorySpace(void);
KERNELAPI MemorySpace_t* KERNELABI GetDomainMemorySpace(void);

/**
 * AreMemorySpacesRelated
 * Checks if two memory spaces are related to each other by sharing resources.
 */
KERNELAPI OsStatus_t KERNELABI
AreMemorySpacesRelated(
        _In_ MemorySpace_t* Space1,
        _In_ MemorySpace_t* Space2);

/**
 * MemorySpaceMap
 * * Creates a new virtual to physical memory mapping.
 * @param MemorySpace           [In]      The memory space where the mapping should be created.
 * @param Address               [In, Out] The virtual address that should be mapped. 
 *                                        Can also be auto assigned if not provided.
 * @param PhysicalAddressValues [In]      Contains physical addresses for the mappings done.
 * @param MemoryFlags           [In]      Memory mapping configuration flags.
 * @param PlacementFlags        [In]      The physical mappings that are allocated are only allowed in this memory mask.
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceMap(
        _In_    MemorySpace_t* MemorySpace,
        _InOut_ VirtualAddress_t*    Address,
        _InOut_ uintptr_t*           PhysicalAddressValues,
        _In_    size_t               Length,
        _In_    unsigned int              MemoryFlags,
        _In_    unsigned int              PlacementFlags);

/**
 * MemorySpaceMapContiguous
 * * Creates a new virtual to contiguous physical memory mapping.
 * @param MemorySpace          [In]      The memory space where the mapping should be created.
 * @param Address              [In, Out] The virtual address that should be mapped. 
 *                                       Can also be auto assigned if not provided.
 * @param PhysicalStartAddress [In]      Contains physical addresses for the mappings done.
 * @param MemoryFlags          [In]      Memory mapping configuration flags.
 * @param PlacementFlags       [In]      The physical mappings that are allocated are only allowed in this memory mask.
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceMapContiguous(
        _In_    MemorySpace_t* MemorySpace,
        _InOut_ VirtualAddress_t*    Address,
        _In_    uintptr_t            PhysicalStartAddress,
        _In_    size_t               Length,
        _In_    unsigned int              MemoryFlags,
        _In_    unsigned int              PlacementFlags);

/**
 * MemorySpaceMapReserved
 * * Marks a virtual region of memory as reserved
 * @param memorySpace          [In]      The memory space where the mapping should be created.
 * @param address              [In, Out] The virtual address that should be mapped.
 *                                       Can also be auto assigned if not provided.
 * @param memoryFlags          [In]      Memory mapping configuration flags.
 * @param placementFlags       [In]      The physical mappings that are allocated are only allowed in this memory mask.
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceMapReserved(
        _In_    MemorySpace_t* memorySpace,
        _InOut_ VirtualAddress_t*    address,
        _In_    size_t               size,
        _In_    unsigned int              memoryFlags,
        _In_    unsigned int              placementFlags);

/**
 * MemorySpaceUnmap
 * * Unmaps a virtual memory region from an address space.
 * @param memorySpace
 * @param address
 * @param size
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceUnmap(
        _In_ MemorySpace_t* memorySpace,
        _In_ VirtualAddress_t     address,
        _In_ size_t               size);

/** 
 * MemorySpaceCommit
 * Commits/finishes an already present memory mapping. If a physical address
 * is not already provided one will be allocated for the mapping.
 * @param memorySpace           [In] The memory space where the mapping should be commited.
 * @param address               [In] The virtual address that should be committed.
 *                                   Any kind of offset into the given page address is ignored.
 * @param physicalAddressValues [In] The dma vector where the physical mappings should be provided.
 * @param size                  [In] Length that should be committed.
 * @param placementFlags        [In] Supports MAPPING_PHYSICAL_* flags.
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceCommit(
        _In_ MemorySpace_t*   memorySpace,
        _In_ VirtualAddress_t address,
        _In_ uintptr_t*       physicalAddressValues,
        _In_ size_t           size,
        _In_ unsigned int     placementFlags);

/**
 * MemorySpaceChangeProtection
 * * Changes the attributes of the given memory range.
 * @param MemorySpace  [In] The addressing space the lookup should take place in
 * @param Address      [In] The virtual address the lookup should start at
 * @param Length       [In] Length of the region that should change attributes.
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceChangeProtection(
        _In_        MemorySpace_t* SystemMemorySpace,
        _InOut_Opt_ VirtualAddress_t     Address,
        _In_        size_t               Length,
        _In_        unsigned int              Attributes,
        _Out_       unsigned int*             PreviousAttributes);

/**
 * CloneMemorySpaceMapping
 * * Clones a region of memory mappings into the address space provided. The new mapping
 * * will automatically be marked COMMIT, PERSISTANT and PROVIDED.
 * @param SourceSpace
 * @param DestinationSpace
 * @param SourceAddress
 * @param DestinationAddress
 * @param Length
 * @param MemoryFlags
 * @param PlacementFlags
 */
KERNELAPI OsStatus_t KERNELABI
CloneMemorySpaceMapping(
        _In_        MemorySpace_t* SourceSpace,
        _In_        MemorySpace_t* DestinationSpace,
        _In_        VirtualAddress_t     SourceAddress,
        _InOut_Opt_ VirtualAddress_t*    DestinationAddress,
        _In_        size_t               Length,
        _In_        unsigned int              MemoryFlags,
        _In_        unsigned int              PlacementFlags);

/**
 * GetMemorySpaceMapping
 * * Converts a virtual address range into the mapped physical range.
 * @param MemorySpace  [In]  The addressing space the lookup should take place in
 * @param Address      [In]  The virtual address the lookup should start at
 * @param PageCount    [In]  The length of the lookup in pages.
 * @param DmaVectorOut [Out] The array to fill with mappings.
 */
KERNELAPI OsStatus_t KERNELABI
GetMemorySpaceMapping(
        _In_  MemorySpace_t*   MemorySpace,
        _In_  VirtualAddress_t Address,
        _In_  int              PageCount,
        _Out_ uintptr_t*       DmaVectorOut);

/* GetMemorySpaceAttributes
 * Reads the attributes for a specific virtual memory address in the given space. */
KERNELAPI unsigned int KERNELABI
GetMemorySpaceAttributes(
        _In_ MemorySpace_t* SystemMemorySpace,
        _In_ VirtualAddress_t     VirtualAddress);

/* IsMemorySpacePageDirty
 * Checks if the given virtual address is dirty (has been written data to). 
 * Returns OsSuccess if the address is dirty. */
KERNELAPI OsStatus_t KERNELABI
IsMemorySpacePageDirty(
        _In_ MemorySpace_t*   SystemMemorySpace,
        _In_ VirtualAddress_t       Address);

/* IsMemorySpacePagePresent
 * Checks if the given virtual address is present. Returns success if the page
 * at the address has a mapping. */
KERNELAPI OsStatus_t KERNELABI
IsMemorySpacePagePresent(
        _In_ MemorySpace_t*   SystemMemorySpace,
        _In_ VirtualAddress_t       Address);

/* GetMemorySpacePageSize
 * Retrieves the memory page-size used by the underlying architecture. */
KERNELAPI size_t KERNELABI
GetMemorySpacePageSize(void);

#endif //!__MEMORY_SPACE_INTERFACE__
