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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
#include <vboot/vboot.h>

DECL_STRUCT(MemoryDescriptor);
DECL_STRUCT(Context);
DECL_STRUCT(PlatformMemoryMapping);

/**
 * MemorySpace Definitions
 * Definitions, bit definitions and magic constants for memory spaces
 */
#define MEMORY_DATACOUNT 4

/**
 * MemorySpace (Type) Definitions
 * Definitions, bit definitions and magic constants for memory spaces
 */
#define MEMORY_SPACE_INHERIT            0x00000001U
#define MEMORY_SPACE_APPLICATION        0x00000002U

/**
 * MemorySpace (Flags) Definitions
 * Definitions, bit definitions and magic constants for memory spaces
 * Default settings for mappings are READ|WRITE is set.
 *
 * MAPPING_TRAPPAGE:
 * Trap pages are purely (at this moment) to support memory fault handlers in userspace. These mappings
 * will be marked MAPPING_TRAPPAGE and thus be not handled in kernel, and instead be sent to the thread as a signal
 * of type SIGSEGV with the address as parameter.
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
#define MAPPING_TRAPPAGE                0x00000400U  // Memory pages should trigger a trpap

#define MAPPING_PHYSICAL_FIXED          0x00000001U  // (Physical) Mappings are supplied

#define MAPPING_VIRTUAL_GLOBAL          0x00000002U  // (Virtual) Mapping is done in global access memory
#define MAPPING_VIRTUAL_PROCESS         0x00000004U  // (Virtual) Mapping is process specific
#define MAPPING_VIRTUAL_THREAD          0x00000008U  // (Virtual) Mapping is thread specific
#define MAPPING_VIRTUAL_FIXED           0x00000010U  // (Virtual) Mapping is supplied
#define MAPPING_VIRTUAL_MASK            0x0000001EU

#define MEMORYSPACE_GET(handle) (MemorySpace_t*)LookupHandleOfType(handle, HandleTypeMemorySpace)

typedef struct MemorySpaceContext MemorySpaceContext_t;

// one per thread
typedef struct MemorySpace {
    UUId_t                ParentHandle;
    unsigned int          Flags;
    uintptr_t             Data[MEMORY_DATACOUNT];
    DynamicMemoryPool_t   ThreadMemory;
    MemorySpaceContext_t* Context;
} MemorySpace_t;

typedef struct MemoryMappingHandler {
    element_t      Header;
    MemorySpace_t* MemorySpace;
    UUId_t         Handle;
    uintptr_t      Address;
    size_t         Length;
} MemoryMappingHandler_t;

/**
 * @brief Initializes the system memory space. This initializes a static version of the
 * system memory space which is the default space the cpu should use for kernel operation.
 *
 * @param memorySpace        [In]
 * @param bootInformation    [In]
 * @param kernelMappings     [In]
 * @return                        Status of the initialization.
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceInitialize(
        _In_ MemorySpace_t*           memorySpace,
        _In_ struct VBoot*            bootInformation,
        _In_ PlatformMemoryMapping_t* kernelMappings);

/**
 * @brief Initialize a new memory space, depending on what user is requesting we
 * might recycle a already existing address space
 *
 *
 */
KERNELAPI OsStatus_t KERNELABI
CreateMemorySpace(
    _In_  unsigned int flags,
    _Out_ UUId_t*      handleOut);

/**
 * @brief Switches the current address space out with the the address space provided
 * for the current cpu.
 *
 * @param memorySpace [In] The memory space that should be switched to.
 */
KERNELAPI void KERNELABI
SwitchMemorySpace(
        _In_ MemorySpace_t* memorySpace);

KERNELAPI UUId_t KERNELABI         GetCurrentMemorySpaceHandle(void);
KERNELAPI MemorySpace_t* KERNELABI GetCurrentMemorySpace(void);
KERNELAPI MemorySpace_t* KERNELABI GetDomainMemorySpace(void);
KERNELAPI size_t KERNELABI         GetMemorySpacePageSize(void);

/**
 * @brief Checks if two memory spaces are related to each other by sharing resources.
 *
 * @param Space1
 * @param Space2
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
AreMemorySpacesRelated(
        _In_ MemorySpace_t* Space1,
        _In_ MemorySpace_t* Space2);

/**
 * @brief Creates a new virtual to physical memory mapping.
 *
 * @param memorySpace           [In]      The memory space where the mapping should be created.
 * @param address               [In, Out] The virtual address that should be mapped.
 *                                        Can also be auto assigned if not provided.
 * @param physicalAddressValues [In]      Contains physical addresses for the mappings done.
 * @param pageMask              [In]      The accepted page mask for physical pages allocated.
 * @param memoryFlags           [In]      Memory mapping configuration flags.
 * @param placementFlags        [In]      The physical mappings that are allocated are only allowed in this memory mask.
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceMap(
        _In_    MemorySpace_t* memorySpace,
        _InOut_ vaddr_t*       address,
        _InOut_ uintptr_t*     physicalAddressValues,
        _In_    size_t         length,
        _In_    size_t         pageMask,
        _In_    unsigned int   memoryFlags,
        _In_    unsigned int   placementFlags);

/**
 * @brief Creates a new virtual to contiguous physical memory mapping.
 *
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
        _InOut_ vaddr_t*       Address,
        _In_    uintptr_t      PhysicalStartAddress,
        _In_    size_t         Length,
        _In_    unsigned int   MemoryFlags,
        _In_    unsigned int   PlacementFlags);

/**
 * @brief Marks a virtual region of memory as reserved
 *
 * @param memorySpace          [In]      The memory space where the mapping should be created.
 * @param address              [In, Out] The virtual address that should be mapped.
 *                                       Can also be auto assigned if not provided.
 * @param memoryFlags          [In]      Memory mapping configuration flags.
 * @param placementFlags       [In]      The physical mappings that are allocated are only allowed in this memory mask.
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceMapReserved(
        _In_    MemorySpace_t* memorySpace,
        _InOut_ vaddr_t*       address,
        _In_    size_t         size,
        _In_    unsigned int   memoryFlags,
        _In_    unsigned int   placementFlags);

/**
 * @brief Unmaps a virtual memory region from an address space.
 *
 * @param memorySpace
 * @param address
 * @param size
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceUnmap(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         size);

/**
 * @brief Commits/finishes an already present memory mapping. If a physical address
 * is not already provided one will be allocated for the mapping.
 *
 * @param memorySpace           [In] The memory space where the mapping should be commited.
 * @param address               [In] The virtual address that should be committed.
 *                                   Any kind of offset into the given page address is ignored.
 * @param physicalAddressValues [In] The dma vector where the physical mappings should be provided.
 * @param size                  [In] Length that should be committed.
 * @param pageMask              [In] The accepted page mask for physical pages allocated.
 * @param placementFlags        [In] Supports MAPPING_PHYSICAL_* flags.
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceCommit(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ uintptr_t*     physicalAddressValues,
        _In_ size_t         size,
        _In_ size_t         pageMask,
        _In_ unsigned int   placementFlags);

/**
 * @brief Changes the attributes of the given memory range.
 *
 * @param memorySpace
 * @param address
 * @param length
 * @param attributes
 * @param previousAttributes
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceChangeProtection(
        _In_        MemorySpace_t* memorySpace,
        _InOut_Opt_ vaddr_t        address,
        _In_        size_t         length,
        _In_        unsigned int   attributes,
        _Out_       unsigned int*  previousAttributes);

/**
 * @brief Clones a region of memory mappings into the address space provided. The new mapping
 * will automatically be marked COMMIT, PERSISTANT and PROVIDED.
 *
 * @param sourceSpace
 * @param destinationSpace
 * @param sourceAddress
 * @param destinationAddress
 * @param length
 * @param memoryFlags
 * @param placementFlags
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceCloneMapping(
        _In_        MemorySpace_t* sourceSpace,
        _In_        MemorySpace_t* destinationSpace,
        _In_        vaddr_t        sourceAddress,
        _InOut_Opt_ vaddr_t*       destinationAddress,
        _In_        size_t         length,
        _In_        unsigned int   memoryFlags,
        _In_        unsigned int   placementFlags);

/**
 * @brief Converts a virtual address range into the mapped physical range.
 *
 * @param memorySpace  [In]  The addressing space the lookup should take place in
 * @param address      [In]  The virtual address the lookup should start at
 * @param pageCount    [In]  The length of the lookup in pages.
 * @param dmaVectorOut [Out] The array to fill with mappings.
 */
KERNELAPI OsStatus_t KERNELABI
GetMemorySpaceMapping(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        address,
        _In_  int            pageCount,
        _Out_ uintptr_t*     dmaVectorOut);

/**
 * @brief Queries allocation information from an existing allocation by its virtual address
 *
 * @param memorySpace
 * @param address
 * @param descriptor
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceQuery(
        _In_ MemorySpace_t*      memorySpace,
        _In_ vaddr_t             address,
        _In_ MemoryDescriptor_t* descriptor);

/**
 * @brief Retrieves the attributes for a specific virtual memory address in the given space.
 *
 * @param memorySpace
 * @param address
 * @param length
 * @param attributesArray
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
GetMemorySpaceAttributes(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         length,
        _In_ unsigned int*  attributesArray);

/**
 * @brief Retrieves whether or not the page has been written to.
 *
 * @param memorySpace [In] The memory space to check the address in.
 * @param address     [In] The address to check for access.
 * @return            Returns OsSuccess if the address is dirty.
 */
KERNELAPI OsStatus_t KERNELABI
IsMemorySpacePageDirty(
        _In_ MemorySpace_t*   memorySpace,
        _In_ vaddr_t       address);

/**
 * @brief Checks if the given virtual address has a physical address and is present.
 *
 * @param memorySpace [In] The memory space to check the address in.
 * @param address     [In] The virtual address to check.
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
IsMemorySpacePagePresent(
        _In_ MemorySpace_t*   memorySpace,
        _In_ vaddr_t       address);

/**
 * @brief Sets the signal handler for the shared memory-space given
 *
 * @param memorySpace          [In] The memory space to update
 * @param signalHandlerAddress [In] The address of the signal handler.
 * @return                     Status of the operation
 */
KERNELAPI OsStatus_t KERNELABI
MemorySpaceSetSignalHandler(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        signalHandlerAddress);

/**
 * @brief Retrieves the signal-handler address of the given memory-space
 *
 * @param memorySpace [In] The memory space to retrieve the signal handler address from.
 * @return            The address of the signal-handler.
 */
KERNELAPI vaddr_t KERNELABI
MemorySpaceSignalHandler(
        _In_ MemorySpace_t* memorySpace);

#endif //!__MEMORY_SPACE_INTERFACE__
