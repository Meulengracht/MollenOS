/**
 * Copyright, Philip Meulengracht
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
 */

#ifndef __DMA_PRIVATE_H__
#define __DMA_PRIVATE_H__

#include "backend.h"
#include <mutex.h>
#include <stdatomic.h>

enum DMAAddressSpaceMode {
    DMAAddressSpaceDirect,
    DMAAddressSpaceTranslated
};

/**
 * @brief State for one IOMMU unit, shared by all the domains it serves.
 *
 * Lock is held around every backend call and protects Spaces, which also
 * counts spaces that are still being created. Failure holds the reason the
 * unit failed. It is set once and never cleared, and OS_EOK means the unit is
 * healthy and accepts new work. Config, Ops and Context never change after
 * creation. The backend must keep Context valid until the code that created
 * the unit has destroyed it successfully.
 */
struct DMAIOMMUUnit {
    Mutex_t              Lock;
    _Atomic(oserr_t)     Failure;
    unsigned int         Spaces;
    DMAIOMMUUnitConfig_t Config;
    DMAIOMMUBackendOps_t Ops;
    void*                Context;
};

/**
 * @brief The rules for turning buffer memory into device addresses.
 *
 * One device context and all of its mappings share a space. Each live mapping
 * holds its own reference. The mode and its data never change after creation.
 * For a direct space, the ranges are stored at the end of this same heap
 * allocation. The per-device address limit is kept by the caller, not here.
 */
struct DMAAddressSpace {
    // The ranges are stored right after this struct, so the struct itself must
    // be aligned for them. On i386 pointer alignment alone could leave that
    // array misaligned.
    _Alignas(struct SHMDeviceRange)
    _Atomic(unsigned int)    References;
    enum DMAAddressSpaceMode Mode;
    union {
        struct {
            uint32_t               Count;
            struct SHMDeviceRange* Ranges;
        } Direct;
        struct {
            DMAIOMMUUnit_t*     Unit;
            DMAIOVAAllocator_t* Allocator;
            void*               BackendSpace;
        } Translated;
    };
};

enum DMAAddressMappingState {
    DMAAddressMappingPrepared,
    DMAAddressMappingInstallStarted,
    DMAAddressMappingInstalled,
    DMAAddressMappingRemoving,
    DMAAddressMappingRemoved
};

typedef struct DMATranslatedMapping DMATranslatedMapping_t;

/**
 * @brief Device segments and a reference to the address space they were built from.
 *
 * The Segments array does not change after preparation. State records how far
 * installation or removal has got. Direction records whether the device may
 * read, write or both. Direct DMA cannot enforce this in hardware. Translated
 * is NULL for direct mappings. Otherwise it holds the IOVA range and the
 * backend data. The caller keeps the physical pages allocated until release
 * succeeds. The caller must not make calls on the same mapping at the same
 * time from different threads.
 */
struct DMAAddressMapping {
    struct DMAAddressSpace*      Space;
    enum DMAAddressMappingState  State;
    enum DMADirection            Direction;
    uint32_t                     SegmentCount;
    DMATranslatedMapping_t*      Translated;
    struct DMASegment            Segments[];
};

/**
 * @brief Build the segments for a direct space, once the common code has checked the arguments.
 *
 * The common code adds the mapping's space reference before calling this and
 * drops it if this fails. This function never changes the reference count. The
 * output pointer must be NULL on entry and is unchanged on error.
 */
KERNELABI oserr_t KERNELAPI
DMADirectMappingPrepare(
    _In_    struct DMAAddressSpace*          space,
    _In_    const SHMSG_t*                   extents,
    _In_    int                              extentCount,
    _In_    uint64_t                         addressLimit,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAAddressMapping**       mappingOut);

/**
 * Reserves one range of device addresses and prepares the backend mapping. It
 * follows the same reference and output rules as DMADirectMappingPrepare, and
 * it copies the extents.
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedMappingPrepare(
    _In_    struct DMAAddressSpace*          space,
    _In_    const SHMSG_t*                   extents,
    _In_    int                              extentCount,
    _In_    uint64_t                         addressLimit,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAAddressMapping**       mappingOut);

/** Installs the mapping once. Checks the unit's health while holding its mutex and updates State. */
KERNELABI oserr_t KERNELAPI
DMATranslatedMappingInstall(
    _In_ struct DMAAddressMapping* mapping);

/**
 * @brief Finish removing the mapping and free its translated data on success.
 * The common code then frees the mapping itself and drops its space reference.
 * On error, all unfinished work and the reserved IOVA range stay in place so
 * the caller can retry.
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedMappingRelease(
    _In_ struct DMAAddressMapping* mapping);

/**
 * @brief Destroy a translated space that has no mappings left and lower the unit's space count.
 * The caller must already have dropped the last space reference. Only on
 * success may the caller free the space struct. If the IOVA allocator reports
 * an inconsistency, the space stays allocated and must not be freed.
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedSpaceDestroy(
    _In_ struct DMAAddressSpace* space);

/**
 * @brief Count device segments or write them into a caller-supplied array.
 *
 * A device segment is a run of bytes with consecutive device addresses, so the
 * device can transfer it as one block. This splits the physical ranges where
 * the device address jumps, and merges pieces that line up.
 *
 * Call it twice. First pass NULL for segmentsOut to get the count, then call
 * again with an array that has room for that many entries. Both calls must get
 * the same inputs. Callers use this when creating direct mappings and when
 * checking segment alignment. The caller must check the pointers and
 * extentCount, and check that each nonzero alignment requirement is a power
 * of two. This function does not.
 *
 * @param space Address space. Hold a reference throughout both calls.
 * @param addressLimit Highest device address allowed, inclusive.
 * @param extents Allocated physical byte ranges in buffer order.
 * @param extentCount Number of entries in extents, at least one.
 * @param requirements Optional alignment requirements, checked by the caller.
 * @param segmentsOut NULL to count only, otherwise receives the device segments.
 * @param countOut Receives the number of segments on success.
 * @return OS_EOK on success. OS_EINVALPARAMS for an invalid physical range.
 *         OS_ENOENT if a byte has no device address. OS_ENOTSUPPORTED for unmet
 *         alignment, a non-direct space, or a segment count that does not fit in uint32_t.
 *         Discard partial output on failure. Neither call allocates memory or
 *         changes hardware address mappings.
 */
KERNELABI oserr_t KERNELAPI
DMADirectWalkSegments(
    _In_  const struct DMAAddressSpace*    space,
    _In_  uint64_t                         addressLimit,
    _In_  const SHMSG_t*                   extents,
    _In_  int                              extentCount,
    _In_  const struct DMAMapRequirements* requirements,
    _Out_ struct DMASegment*               segmentsOut,
    _Out_ uint32_t*                        countOut);

#endif //!__DMA_PRIVATE_H__
