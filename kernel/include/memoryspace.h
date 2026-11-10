/**
 * Copyright 2023, Philip Meulengracht
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

#include <arch/platform.h>
#include <ddk/types/capabilities.h>
#include <os/types/memory.h>
#include <utils/dynamic_memory_pool.h>
#include <vboot/vboot.h>

DECL_STRUCT(PlatformMemoryMapping);
DECL_STRUCT(HandleGrantsTable);
DECL_STRUCT(SHMView);


/**
 * MemorySpace (Type) Definitions
 * Definitions, bit definitions and magic constants for memory spaces
 */
#define MEMORY_SPACE_INHERIT            0x00000001U
#define MEMORY_SPACE_APPLICATION        0x00000002U

/**
 * MemorySpace (Flags) Definitions
 * Definitions, bit definitions and magic constants for memory spaces
 * Mappings are readable and writable by default.
 *
 * MAPPING_TRAPPAGE:
 * Trap pages currently support memory fault handlers in userspace. For MAPPING_TRAPPAGE
 * mappings, the kernel sends the fault to the thread as SIGSEGV with the fault address
 * as a parameter instead of handling the fault.
 */
#define MAPPING_USERSPACE               0x00000001U  // Userspace mapping
#define MAPPING_NOCACHE                 0x00000002U  // Disable caching for mapping
#define MAPPING_READONLY                0x00000004U  // Memory can only be read
#define MAPPING_EXECUTABLE              0x00000008U  // Memory can be executed
#define MAPPING_ISDIRTY                 0x00000010U  // Memory marked as written to
#define MAPPING_PERSISTENT              0x00000020U  // Memory should not be freed when mapping is removed
#define MAPPING_DOMAIN                  0x00000040U  // Memory allocated for mapping must be domain local
#define MAPPING_COMMIT                  0x00000080U  // Memory should be committed immediately
#define MAPPING_CLEAN                   0x00000400U  // Memory should be zeroed when underlying physical pages are allocated
#define MAPPING_STACK                   0x00000100U  // Memory resource is a stack and needs a guard page
#define MAPPING_TRAPPAGE                0x00000200U  // Memory pages should trigger a trap
#define MAPPING_DEVICE                  0x00000800U  // Device registers, not ordinary uncached RAM

#define MAPPING_PHYSICAL_FIXED          0x00000001U  // (Physical) Mappings are supplied
#define MAPPING_PHYSICAL_CONTIGUOUS     0x00000002U  // (Physical) Mapping must be physically contiguous
#define MAPPING_PHYSICAL_MASK           0x0000000FU

#define MAPPING_VIRTUAL_GLOBAL          0x00000010U  // (Virtual) Mapping is done in global access memory
#define MAPPING_VIRTUAL_PROCESS         0x00000020U  // (Virtual) Mapping is process specific
#define MAPPING_VIRTUAL_THREAD          0x00000030U  // (Virtual) Mapping is thread specific
#define MAPPING_VIRTUAL_FIXED           0x00000040U  // (Virtual) Mapping is supplied
#define MAPPING_VIRTUAL_MASK            0x000000F0U

#define MEMORYSPACE_GET(handle) (MemorySpace_t*)LookupHandle(handle, HandleTypeMemorySpace)

/**
 * @brief Security state shared by the thread/memoryspace clusters.
 */
typedef struct MSContextSecurity {
    uuid_t               GlobalHandleID;
    Capabilities_t       Capabilities;
    HandleGrantsTable_t* HandleTable;
} MSContextSecurity_t;

/**
 * @brief Per-thread address space belonging to a shared memory-space group.
 */
typedef struct MemorySpace {
    // The group contains resources shared by its memory spaces.
    uuid_t                GroupGlobalHandleID;
    uuid_t                ParentHandle;
    unsigned int          Flags;
    DynamicMemoryPool_t   ThreadMemory;
    PlatformMemoryBlock_t PlatformData;
} MemorySpace_t;

/**
 * @brief Initializes the static system memory space. This is the default address
 * space the CPU should use for kernel operation.
 *
 * @param memorySpace        [In]
 * @param bootInformation    [In]
 * @param kernelMappings     [In]
 * @return                        Status of the initialization.
 */
KERNELAPI oserr_t KERNELABI
MemorySpaceInitialize(
    _In_ MemorySpace_t*           memorySpace,
    _In_ struct VBoot*            bootInformation,
    _In_ PlatformMemoryMapping_t* kernelMappings);

/**
 * @brief
 * @param flags            Memory-space creation flags.
 * @param msHandleOut      Receives the memory-space handle on success.
 * @param securityGroupOut Receives the security-group ID on success.
 * @return                 Status of the operation.   
 */
KERNELAPI oserr_t KERNELABI
CreateMemorySpace(
    _In_  unsigned int flags,
    _Out_ uuid_t*      msHandleOut,
    _Out_ uuid_t*      securityGroupOut);

/**
 * @brief Switches the current CPU to the given address space.
 *
 * @param memorySpace [In] The memory space that should be switched to.
 */
KERNELAPI void KERNELABI
MemorySpaceSwitch(
    _In_ MemorySpace_t* memorySpace);

KERNELAPI uuid_t KERNELABI         GetCurrentMemorySpaceHandle(void);
KERNELAPI MemorySpace_t* KERNELABI GetCurrentMemorySpace(void);
KERNELAPI MemorySpace_t* KERNELABI GetDomainMemorySpace(void);
KERNELAPI size_t KERNELABI         GetMemorySpacePageSize(void);

/**
 * @brief Returns the security context shared by the current memory-space group.
 * The caller must not free the context.
 * 
 * @return Pointer valid while the current memory space is alive, or NULL if it
 * has no group.
 */
KERNELAPI MSContextSecurity_t* KERNELABI MemorySpaceGetSecurityContext(void);

/**
 * @brief Initializes the memory space synchronization system.
 */
KERNELAPI void KERNELABI MemorySpaceSyncInitialize(void);

/**
 * @brief Processes any pending memory synchronization requests for the current CPU.
 */
KERNELAPI void KERNELABI MemorySpaceSyncProcessPending(void);

/**
 * @brief Checks if two memory spaces are related to each other by sharing resources.
 *
 * @param Space1
 * @param Space2
 * @return
 */
KERNELAPI oserr_t KERNELABI
AreMemorySpacesRelated(
    _In_ MemorySpace_t* Space1,
    _In_ MemorySpace_t* Space2);

/**
 * @brief Addresses, flags and shared memory information for a mapping.
 */
struct MemorySpaceMapOptions {
    // SHMTag identifies the shared memory region for internal bookkeeping.
    // Supply it only for SHM mappings.
    uuid_t SHMTag;
    // VirtualStart is the start of the virtual mapping. The caller must supply
    // it when mapping a specific address or resizing an existing mapping.
    vaddr_t VirtualStart;
    // PhysicalStart is the physical address used for contiguous mappings.
    // Use Pages for other mappings.
    paddr_t PhysicalStart;
    // Pages points to an array of physical page addresses. With MAPPING_PHYSICAL_FIXED,
    // the array supplies the pages to map. Otherwise, the mapping fills it with
    // the addresses of the allocated pages.
    // The array must have Length / PAGE_SIZE entries.
    paddr_t* Pages;
    // Length is the length of the mapping to create. If updating an existing mapping
    // this is the new length of the mapping.
    size_t Length;
    // Mask is used to allocate physical pages when none are supplied.
    size_t Mask;
    unsigned int Flags;
    unsigned int PlacementFlags;
    // Optional restricted shared memory view.
    // The mapping adds a separate reference before its pages are visible.
    SHMView_t* SHMView;
    uuid_t     SHMViewID;
    size_t     SHMOffset;
};

/**
 * @brief Creates a new virtual to physical memory mapping.
 * @param[In]  memorySpace The memory space where the mapping should be created.
 * @param[In]  options     Configuration options for the mapping
 * @param[Out] mappingOut  The resulting virtual memory mapping.
 */
KERNELAPI oserr_t KERNELABI
MemorySpaceMap(
    _In_  MemorySpace_t*                memorySpace,
    _In_  struct MemorySpaceMapOptions* options,
    _Out_ vaddr_t*                      mappingOut);

/**
 * @brief Unmaps a virtual memory region from an address space.
 *
 * @param memorySpace
 * @param address
 * @param size
 */
KERNELAPI oserr_t KERNELABI
MemorySpaceUnmap(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         size);

/**
 * @brief Commits an existing memory mapping. Allocates physical memory if no
 * physical address is supplied.
 *
 * @param memorySpace           [In] The memory space where the mapping should be committed.
 * @param address               [In] The virtual address that should be committed.
 *                                   Any kind of offset into the given page address is ignored.
 * @param physicalAddressValues [In] The DMA vector where the physical mappings should be provided.
 * @param size                  [In] Length that should be committed.
 * @param pageMask              [In] The accepted page mask for physical pages allocated.
 * @param placementFlags        [In] Supports MAPPING_PHYSICAL_* flags.
 */
KERNELAPI oserr_t KERNELABI
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
KERNELAPI oserr_t KERNELABI
MemorySpaceChangeProtection(
    _In_  MemorySpace_t* memorySpace,
    _In_  vaddr_t        address,
    _In_  size_t         length,
    _In_  unsigned int   attributes,
    _Out_ unsigned int*  previousAttributes);

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
KERNELAPI oserr_t KERNELABI
MemorySpaceCloneMapping(
    _In_    MemorySpace_t* sourceSpace,
    _In_    MemorySpace_t* destinationSpace,
    _In_    vaddr_t        sourceAddress,
    _InOut_ vaddr_t*       destinationAddress,
    _In_    size_t         length,
    _In_    unsigned int   memoryFlags,
    _In_    unsigned int   placementFlags);

/**
 * @brief Copies readable userspace memory into kernel memory.
 *
 * The caller must run in the memory space of the source thread, and all user
 * pages in the range must be committed RAM. Mapping changes are blocked until
 * the copy is done. Other threads can still write to the source while it is
 * being copied.
 *
 * @param kernelBuffer Kernel destination with room for length bytes.
 * @param userBuffer Userspace source in the current memory space.
 * @param length Nonzero number of bytes to copy.
 * @return OS_EOK, OS_EINVALPARAMS for an invalid range, or a page lookup error.
 */
KERNELAPI oserr_t KERNELABI
MemorySpaceCopyFromUser(
    _Out_ void*       kernelBuffer,
    _In_  const void* userBuffer,
    _In_  size_t      length);

/**
 * @brief Copies kernel memory into writable userspace memory.
 *
 * The caller must run in the memory space of the destination thread, and all
 * user pages in the range must be committed RAM. The whole range is checked
 * before anything is written, and mapping changes are blocked until the copy
 * is done.
 *
 * @param userBuffer Userspace destination in the current memory space.
 * @param kernelBuffer Kernel source containing length bytes.
 * @param length Nonzero number of bytes to copy.
 * @return OS_EOK, OS_EINVALPARAMS for an invalid range, or a page lookup error.
 */
KERNELAPI oserr_t KERNELABI
MemorySpaceCopyToUser(
    _Out_ void*       userBuffer,
    _In_  const void* kernelBuffer,
    _In_  size_t      length);

/**
 * @brief Converts a virtual address range into the mapped physical range.
 *
 * @param memorySpace  [In]  The address space the lookup should take place in.
 * @param address      [In]  The virtual address the lookup should start at.
 * @param pageCount    [In]  The length of the lookup in pages.
 * @param dmaVectorOut [Out] The array to fill with mappings.
 */
KERNELAPI oserr_t KERNELABI
GetMemorySpaceMapping(
    _In_  MemorySpace_t* memorySpace,
    _In_  vaddr_t        address,
    _In_  int            pageCount,
    _Out_ uintptr_t*     dmaVectorOut);

/**
 * @brief Queries allocation information by virtual address.
 *
 * @param memorySpace
 * @param address
 * @param descriptor
 * @return
 */
KERNELAPI oserr_t KERNELABI
MemorySpaceQuery(
    _In_ MemorySpace_t*        memorySpace,
    _In_ vaddr_t               address,
    _In_ OSMemoryDescriptor_t* descriptor);

/**
 * @brief Retrieves the attributes for a specific virtual memory address in the given space.
 *
 * @param memorySpace
 * @param address
 * @param length
 * @param attributesArray
 * @return
 */
KERNELAPI oserr_t KERNELABI
GetMemorySpaceAttributes(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         length,
    _In_ unsigned int*  attributesArray);

/**
 * @brief Sets the signal handler for the given shared memory space.
 *
 * @param memorySpace          [In] The memory space to update.
 * @param signalHandlerAddress [In] The address of the signal handler.
 * @return                     Status of the operation.
 */
KERNELAPI oserr_t KERNELABI
MemorySpaceSetSignalHandler(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        signalHandlerAddress);

/**
 * @brief Retrieves the signal handler address of the given memory space.
 *
 * @param memorySpace [In] The memory space to retrieve the signal handler address from.
 * @return            The address of the signal handler.
 */
KERNELAPI vaddr_t KERNELABI
MemorySpaceSignalHandler(
    _In_ MemorySpace_t* memorySpace);

/**
 * @brief Adds a reference to a security group, so its security state stays
 * valid while the caller checks its grants. Release the reference again with
 * ReleaseGlobalHandle(groupID).
 *
 * @param groupID     Full group ID from the IPC message. Values that do not fit
 *                    in a handle ID are rejected.
 * @param securityOut Receives the security state of the group. It stays valid
 *                    until the reference is released.
 * @return OS_EOK, OS_EINVALPARAMS for an invalid group ID, or a lookup error if
 *         the group does not exist.
 */
oserr_t
MSContextAcquireSecurity(
    _In_  uint64_t              groupID,
    _Out_ MSContextSecurity_t** securityOut);

#endif //!__MEMORY_SPACE_INTERFACE__
