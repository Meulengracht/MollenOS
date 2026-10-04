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
 *
 *
 * Persistent packet pools and private lease-state tracking.
 * 
 * Persistent TX/RX buffers for netd.
 *
 * See buffers.h for the rules about the executor and about who manages each lease.
 * Every state change is checked in full before any counters are modified, so a
 * request that is refused leaves the state untouched.
 */

#include "buffers.h"
#include <os/handle.h>
#include <os/memory.h>
#include <os/shm.h>
#include <stdlib.h>
#include <string.h>

// Special value indicating no slot is available.
#define __NO_SLOT UINT32_MAX

// We have two pools: one for TX and one for RX.
// 0 = TX
// 1 = RX
#define __POOL_COUNT 2
#define __POOL_TX    0
#define __POOL_RX    1

enum __PoolState {
    POOL_LOCAL,
    POOL_REGISTERING,
    POOL_REGISTERED,
    POOL_RETIRED
};

enum __SlotState {
    SLOT_FREE,
    SLOT_HELD,
    SLOT_PENDING,
    SLOT_DRIVER,
    SLOT_READY
};

/**
 * @brief Tracks ownership and completion data for one shared-memory slot.
 */
struct __BufferSlot {
    enum __SlotState State;
    uint32_t         Next;
    uint64_t         Sequence;
    uint32_t         SubmittedLength;

    // Set when prepared; a lease is submitted at most once, even if rejected.
    bool Used;

    // Admission reply not applied yet; the completion may arrive first.
    bool    AwaitingAdmission;
    bool    AdmissionKnown;
    oserr_t AdmissionStatus;

    // Completion from the driver, may arrive before or after the admission reply.
    struct ctt_netadapter_completion Completion;
};

/**
 * @brief Holds one direction's shared-memory region and slot ownership state.
 */
struct __BufferPool {
    // The handle to the shared memory region for this pool
    OSHandle_t Handle;
    void*      Base;

    struct ctt_netadapter_pool Description;

    struct __BufferSlot* Slots;
    enum __PoolState       State;

    uint32_t             Id;
    uint32_t             FreeHead;
    uint32_t             FreeCount;

    // PENDING plus DRIVER slots. A completion arriving before its
    // admission reply decrements this; the reply then does not.
    uint32_t Outstanding;
};

/**
 * @brief Stores a completion until all earlier completion sequences are present.
 */
struct __CompletionEntry {
    // A zero sequence marks an empty entry. The table has a fixed size (the driver's
    // max_unacked_completions) and is indexed by completion sequence, so completions
    // that arrive out of order can be stored until the missing earlier ones arrive.
    struct ctt_netadapter_completion Record;
};

/**
 * @brief Holds checked slot dimensions and shared-memory sizes for both pools.
 */
struct __BufferLayout {
    uint32_t DataOffset;
    uint32_t SlotSize;
    uint32_t SlotCounts[__POOL_COUNT];
    uint64_t PoolBytes[__POOL_COUNT];
};

/**
 * @brief Tracks the session's pools, leases, and completion acknowledgement window.
 */
struct NetBufferManager {
    struct ctt_netadapter_session Session;
    struct __BufferPool           Pools[__POOL_COUNT];

    struct __CompletionEntry* Window;
    uint32_t                  WindowSize;
    uint32_t                  Capacity;
    uint32_t                  DataOffset;
    uint32_t                  OutstandingLimit[__POOL_COUNT];

    // Completions not yet acknowledged; the driver must keep
    // room for each of them until we acknowledge it.
    uint32_t UnackedCount;     

    uint64_t Processed;
    uint64_t Acknowledged;
    uint64_t Highest;
    bool     Closed;
};

static int
__PoolIndex(
    _In_ enum ctt_netadapter_direction direction)
{
    if (direction == CTT_NETADAPTER_DIRECTION_TX) {
        return __POOL_TX;
    }
    if (direction == CTT_NETADAPTER_DIRECTION_RX) {
        return __POOL_RX;
    }
    return -1;
}

static bool
__SameSession(
    _In_ const NetBufferManager_t*            manager,
    _In_ const struct ctt_netadapter_session* session)
{
    if (manager == NULL || session == NULL) {
        return false;
    }

    // A session matches if bothe the ID and generation are the same.
    return manager->Session.id == session->id &&
           manager->Session.generation == session->generation;
}

static bool
__RoundUpAligned(
    _In_  uint64_t  value,
    _In_  uint64_t  alignment,
    _Out_ uint64_t* result)
{
    // Check that the alignment is a power of two.
    if (!alignment || (alignment & (alignment - 1))) {
        return false;
    }
    
    // Check for potential overflow before rounding up.
    if (value > UINT64_MAX - (alignment - 1)) {
        return false;
    }
    
    // Round up the value to the nearest multiple of the alignment.
    *result = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

// Returns true if allocation happened.
static bool
__AllocateBudget(
    _InOut_ uint64_t* total,
    _In_    uint64_t  amount,
    _In_    uint64_t  budget)
{
    if (*total > budget || amount > (budget - *total)) {
        return false;
    }
    
    *total += amount;
    return true;
}

static void
__FreeManager(
    _InOut_ NetBufferManager_t* manager)
{
    int i;

    // Only called when Create fails, or from Destroy after it has checked that the
    // driver no longer uses the pools and that every lease has been released.
    for (i = 0; i < __POOL_COUNT; ++i) {
        if (manager->Pools[i].Handle.ID != UUID_INVALID) {
            OSHandleDestroy(&manager->Pools[i].Handle);
        }
        free(manager->Pools[i].Slots);
    }
    free(manager->Window);
    free(manager);
}

static oserr_t
__ValidateInfo(
    _In_ const struct ctt_netadapter_info* info,
    _In_ size_t                          pageSize)
{
    if (info->min_version > 2 || info->max_version < 2) {
        return OS_EINVALPARAMS;
    }
    if (info->framing != CTT_NETADAPTER_FRAMING_ETHERNET) {
        return OS_EINVALPARAMS;
    }
    if (info->max_queue_pairs != 1 || info->max_segments != 1 || info->max_pools < __POOL_COUNT) {
        return OS_EINVALPARAMS;
    }

    if (!info->max_outstanding_tx || !info->max_outstanding_rx) {
        return OS_EINVALPARAMS;
    }
    if (info->max_outstanding_rx < info->min_rx_slots) {
        return OS_EINVALPARAMS;
    }
    if (info->max_unacked_completions < 2 ||
        info->max_unacked_completions <= info->min_rx_slots) {
        return OS_EINVALPARAMS;
    }
    if (!info->max_batch_size) {
        return OS_EINVALPARAMS;
    }

    if (!info->buffer_alignment || info->buffer_alignment > pageSize) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__ValidateConfig(
    _In_ const struct ctt_netadapter_info* info,
    _In_ const NetBufferConfig_t*         config)
{
    if (!config->TxSlots || !config->RxSlots) {
        return OS_EINVALPARAMS;
    }
    if (config->RxSlots < info->min_rx_slots) {
        return OS_EINVALPARAMS;
    }
    if (config->FrameCapacity < NET_ADAPTER_ETHERNET_HEADER_SIZE || 
        config->FrameCapacity > info->max_frame_size) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__CalculateBufferLayout(
    _In_  const struct ctt_netadapter_info* info,
    _In_  const NetBufferConfig_t*         config,
    _In_  size_t                          pageSize,
    _Out_ struct __BufferLayout*           layout)
{
    uint64_t offset, stride, total = sizeof(NetBufferManager_t);
    uint64_t slotBytes, windowBytes;
    bool     valid;
    int      i;

    // Leave the requested headroom before the frame data, then round that offset up so
    // the data begins at the alignment required by the device. The shared-memory base
    // is page-aligned, so alignments up to one page work here; the driver still checks
    // any additional rules it has for accessing memory.
    valid = __RoundUpAligned(info->required_headroom, info->buffer_alignment, &offset);
    if (!valid) {
        return OS_EOVERFLOW;
    }
    
    // Each slot also needs room for the frame and any required space after it. Round
    // the full slot size up so the next slot starts at the required alignment too.
    valid = __RoundUpAligned(
        offset + config->FrameCapacity + info->required_tailroom,
        info->buffer_alignment,
        &stride
    );
    if (!valid) {
        return OS_EOVERFLOW;
    }
    
    // The pool description stores the slot size in 32 bits, so a larger value cannot
    // be represented even if the calculations above succeeded.
    if (stride > UINT32_MAX) {
        return OS_EOVERFLOW;
    }

    // Work out both pool sizes before allocating anything. This lets us reject an
    // oversized request up front, without having to undo a partly created setup.
    layout->SlotCounts[0] = config->TxSlots;
    layout->SlotCounts[1] = config->RxSlots;
    for (i = 0; i < __POOL_COUNT; ++i) {
        // The device advertises a maximum slot count for each pool separately.
        if (layout->SlotCounts[i] > info->max_slots_per_pool) {
            return OS_EOVERFLOW;
        }

        // Make the shared-memory region large enough for every slot, then round the
        // region size to whole pages because shared memory is allocated page by page.
        if (!__RoundUpAligned(stride * layout->SlotCounts[i], pageSize, &layout->PoolBytes[i])) {
            return OS_EOVERFLOW;
        }

        // The region must fit both in this process's size type and within the device's
        // per-pool limit.
        if (layout->PoolBytes[i] > SIZE_MAX || layout->PoolBytes[i] > info->max_pool_bytes) {
            return OS_EOVERFLOW;
        }

        // Alongside the shared bytes, each slot needs a local record for its state and
        // ownership information. Count these records in the overall memory budget too.
        slotBytes = (uint64_t)layout->SlotCounts[i] * sizeof(struct __BufferSlot);
        if (slotBytes > SIZE_MAX) {
            return OS_EOVERFLOW;
        }
        if (!__AllocateBudget(&total, layout->PoolBytes[i], config->MemoryBudget)) {
            return OS_EOVERFLOW;
        }
        if (!__AllocateBudget(&total, slotBytes, config->MemoryBudget)) {
            return OS_EOVERFLOW;
        }
    }
    
    // The device limits the combined shared-memory size of the registered TX and RX
    // pools, not just each pool on its own.
    if (layout->PoolBytes[0] > info->max_registered_bytes) {
        return OS_EOVERFLOW;
    }
    
    if (layout->PoolBytes[1] > info->max_registered_bytes - layout->PoolBytes[0]) {
        return OS_EOVERFLOW;
    }

    // Keep one tracking record for each completion the device may leave unacknowledged.
    // This memory is local bookkeeping, so it counts toward our budget but not the
    // device's registered shared-memory limit.
    windowBytes = (uint64_t)info->max_unacked_completions * sizeof(struct __CompletionEntry);
    if (windowBytes > SIZE_MAX) {
        return OS_EOVERFLOW;
    }
    
    if (!__AllocateBudget(&total, windowBytes, config->MemoryBudget)) {
        return OS_EOVERFLOW;
    }

    // Save the checked values for pool creation and for locating frame data in a slot.
    layout->DataOffset = (uint32_t)offset;
    layout->SlotSize = (uint32_t)stride;
    return OS_EOK;
}

static oserr_t
__InitializePool(
    _InOut_ struct __BufferPool*             pool,
    _In_    const struct ctt_netadapter_info* info,
    _In_    const struct __BufferLayout*      layout,
    _In_    int                              index)
{
    uint32_t slot;
    uint32_t count = layout->SlotCounts[index];
    uint64_t bytes = layout->PoolBytes[index];
    oserr_t  status;

    pool->Slots = calloc(count, sizeof(*pool->Slots));
    if (!pool->Slots) {
        return OS_EOOM;
    }

    status = SHMCreate(
        &(SHM_t) {
            .Flags = SHM_DEVICE | SHM_CLEAN,
            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
            .Conformity = OSMEMORYCONFORMITY_NONE,
            .Size = (size_t)bytes
        },
        &pool->Handle
    );
    if (status != OS_EOK) {
        return status;
    }

    pool->Base = SHMBuffer(&pool->Handle);
    if (!pool->Base || SHMBufferLength(&pool->Handle) < bytes ||
        ((uintptr_t)pool->Base & (info->buffer_alignment - 1))) {
        return OS_EBUFFER;
    }

    pool->Description = (struct ctt_netadapter_pool) {
        .direction = index == 0 ? CTT_NETADAPTER_DIRECTION_TX : CTT_NETADAPTER_DIRECTION_RX,
        .buffer_handle = pool->Handle.ID,
        .region_offset = 0,
        .region_size = bytes,
        .slot_size = layout->SlotSize,
        .slot_count = count
    };
    pool->FreeHead = 0;
    pool->FreeCount = count;
    for (slot = 0; slot < count; ++slot) {
        pool->Slots[slot].Next = slot + 1 == count ? __NO_SLOT : slot + 1;
    }
    return OS_EOK;
}

oserr_t
NetBuffersCreate(
    _In_  const struct ctt_netadapter_session* session,
    _In_  const struct ctt_netadapter_info*    info,
    _In_  const NetBufferConfig_t*             config,
    _Out_ NetBufferManager_t**                 managerOut)
{
    struct __BufferLayout layout;
    size_t                pageSize = MemoryPageSize();
    NetBufferManager_t*    manager;
    oserr_t               status;
    int                   i;

    if (!managerOut) {
        return OS_EINVALPARAMS;
    }
    *managerOut = NULL;

    if (!session || !info || !config) {
        return OS_EINVALPARAMS;
    }

    if (!session->id || !session->generation) {
        return OS_EINVALPARAMS;
    }

    status = __ValidateInfo(info, pageSize);
    if (status != OS_EOK) {
        return status;
    }

    status = __ValidateConfig(info, config);
    if (status != OS_EOK) {
        return status;
    }

    status = __CalculateBufferLayout(info, config, pageSize, &layout);
    if (status != OS_EOK) {
        return status;
    }

    manager = calloc(1, sizeof(NetBufferManager_t));
    if (!manager) {
        return OS_EOOM;
    }
    
    manager->Session = *session;
    manager->Capacity = config->FrameCapacity;
    manager->DataOffset = layout.DataOffset;
    manager->WindowSize = info->max_unacked_completions;
    manager->OutstandingLimit[0] = info->max_outstanding_tx;
    manager->OutstandingLimit[1] = info->max_outstanding_rx;
    
    // If receive buffers could fill every unacknowledged-completion space in the driver,
    // no TX could be sent, not even the TX (ARP, for example) needed to get any traffic
    // coming in. Post at most one less RX so there is always room for a TX.
    if (manager->OutstandingLimit[1] >= info->max_unacked_completions) {
        manager->OutstandingLimit[1] = info->max_unacked_completions - 1;
    }
    
    manager->Window = calloc(manager->WindowSize, sizeof(*manager->Window));
    if (!manager->Window) {
        __FreeManager(manager);
        return OS_EOOM;
    }

    for (i = 0; i < __POOL_COUNT; ++i) {
        status = __InitializePool(&manager->Pools[i], info, &layout, i);
        if (status != OS_EOK) {
            __FreeManager(manager);
            return status;
        }
    }
    *managerOut = manager;
    return OS_EOK;
}

oserr_t
NetBuffersDestroy(
    _InOut_ NetBufferManager_t** manager)
{
    struct __BufferPool* pool;
    int                  i;

    if (!manager) {
        return OS_EINVALPARAMS;
    }
    
    if (!*manager) {
        return OS_EOK;
    }
    
    for (i = 0; i < __POOL_COUNT; ++i) {
        pool = &(*manager)->Pools[i];
        if ((pool->State != POOL_LOCAL && pool->State != POOL_RETIRED) ||
            pool->FreeCount != pool->Description.slot_count)
            return OS_EBUSY;
    }
    
    __FreeManager(*manager);
    *manager = NULL;
    return OS_EOK;
}

oserr_t
NetBuffersBeginRegistration(
    _InOut_ NetBufferManager_t*            manager,
    _In_    enum ctt_netadapter_direction  direction,
    _Out_   uint64_t*                      registrationOut,
    _Out_   struct ctt_netadapter_pool*    poolOut)
{
    int                  index = __PoolIndex(direction);
    struct __BufferPool* pool;

    if (!manager || index < 0 || !registrationOut || !poolOut) {
        return OS_EINVALPARAMS;
    }
    
    pool = &manager->Pools[index];
    if (manager->Closed || pool->State == POOL_RETIRED) {
        return OS_ENOENT;
    }
    
    // We mark that the pool is in the process of being registered, in
    // case something goes wrong after the RPC to indicate that we already
    // have access to the shared memory.
    if (pool->State == POOL_LOCAL) {
        pool->State = POOL_REGISTERING;
    }
    
    *registrationOut = (uint64_t)index + 1;
    *poolOut = pool->Description;
    return OS_EOK;
}

oserr_t
NetBuffersRegistered(
    _InOut_ NetBufferManager_t*                  manager,
    _In_    const struct ctt_netadapter_session* session,
    _In_    enum ctt_netadapter_direction        direction,
    _In_    uint32_t                             poolId)
{
    int                  index = __PoolIndex(direction);
    struct __BufferPool* pool;

    if (!__SameSession(manager, session) || index < 0 || !poolId) {
        return OS_EINVALPARAMS;
    }
    
    pool = &manager->Pools[index];
    if (manager->Closed || pool->State == POOL_RETIRED) {
        return OS_ENOENT;
    }
    
    if (pool->State == POOL_REGISTERED) {
        return pool->Id == poolId ? OS_EOK : OS_EPROTOCOL;
    }
    
    if (pool->State != POOL_REGISTERING || manager->Pools[1 - index].Id == poolId) {
        return OS_EPROTOCOL;
    }
    
    pool->Id = poolId;
    pool->State = POOL_REGISTERED;
    return OS_EOK;
}

oserr_t
NetBuffersUnregistered(
    _InOut_ NetBufferManager_t*                  manager,
    _In_    const struct ctt_netadapter_session* session,
    _In_    enum ctt_netadapter_direction        direction)
{
    int                  index = __PoolIndex(direction);
    struct __BufferPool* pool;

    if (!__SameSession(manager, session) || index < 0) {
        return OS_EINVALPARAMS;
    }
    
    pool = &manager->Pools[index];
    if (pool->State == POOL_RETIRED) {
        return OS_EOK;
    }
    
    if (pool->State != POOL_REGISTERED) {
        return OS_EINVALPARAMS;
    }
    
    if (pool->Outstanding || manager->Acknowledged != manager->Highest) {
        return OS_EBUSY;
    }
    
    // A completion may arrive (meaning the driver is done with the buffer) before the
    // admission reply for it. Keep the pool registered until that reply has arrived and
    // been matched to its slot.
    for (uint32_t i = 0; i < pool->Description.slot_count; ++i) {
        if (pool->Slots[i].AwaitingAdmission) {
            return OS_EBUSY;
        }
    }
    
    pool->State = POOL_RETIRED;
    return OS_EOK;
}

static NetBufferLease_t
__MakeLease(
    _In_ const NetBufferManager_t*  manager,
    _In_ const struct __BufferPool* pool,
    _In_ uint32_t                   slot)
{
    return (NetBufferLease_t) { 
        manager->Session,
        pool->Description.direction,
        slot,
        pool->Slots[slot].Sequence
    };
}

static struct __BufferSlot*
__LookupLease(
    _In_  NetBufferManager_t*     manager,
    _In_  const NetBufferLease_t* lease,
    _Out_ struct __BufferPool**   poolOut)
{
    struct __BufferPool* pool;
    struct __BufferSlot* slot;
    int                  index;

    if (!lease || !__SameSession(manager, &lease->Session)) {
        return NULL;
    }
    
    index = __PoolIndex(lease->Direction);
    if (index < 0) {
        return NULL;
    }
    
    pool = &manager->Pools[index];
    if (lease->Slot >= pool->Description.slot_count) {
        return NULL;
    }
    
    slot = &pool->Slots[lease->Slot];
    if (!lease->Sequence || slot->Sequence != lease->Sequence) {
        return NULL;
    }
    
    if (slot->State == SLOT_FREE) {
        return NULL;
    }
    
    *poolOut = pool;
    return slot;
}

oserr_t
NetBuffersAcquire(
    _InOut_ NetBufferManager_t*           manager,
    _In_    enum ctt_netadapter_direction direction,
    _Out_   NetBufferLease_t*             leaseOut)
{
    int                  index = __PoolIndex(direction);
    struct __BufferPool* pool;
    struct __BufferSlot* slot;
    uint32_t             id;
    uint64_t             sequence;

    if (!manager || index < 0 || !leaseOut) {
        return OS_EINVALPARAMS;
    }
    
    pool = &manager->Pools[index];
    if (manager->Closed || pool->State != POOL_REGISTERED) {
        return OS_ENOENT;
    }
    
    if (!pool->FreeCount) {
        return OS_EBUSY;
    }
    
    id = pool->FreeHead;
    slot = &pool->Slots[id];
    if (slot->Sequence == UINT64_MAX) {
        return OS_EOVERFLOW;
    }
    
    pool->FreeHead = slot->Next;
    pool->FreeCount--;
    
    // Advance the lease generation before returning this slot so an older lease cannot
    // become valid again after the slot is reused.
    sequence = slot->Sequence + 1;
    
    // Reset the slot
    memset(slot, 0, sizeof(*slot));
    slot->Sequence = sequence;
    slot->State = SLOT_HELD;
    
    // Padding is cleared as well as data: no previous frame bytes are exposed to
    // the driver or accidentally transmitted if it has to pad a short frame.
    memset(
        (uint8_t*)pool->Base + (size_t)id * pool->Description.slot_size, 
        0,
        pool->Description.slot_size
    );
    
    *leaseOut = __MakeLease(manager, pool, id);
    return OS_EOK;
}

static bool
__LocallyManaged(
    _In_ const struct __BufferSlot* slot)
{
    return (slot->State == SLOT_HELD || slot->State == SLOT_READY) 
                && !slot->AwaitingAdmission;
}

oserr_t
NetBuffersView(
    _In_  NetBufferManager_t*     manager,
    _In_  const NetBufferLease_t* lease,
    _Out_ NetBufferView_t*        viewOut)
{
    struct __BufferPool* pool;
    struct __BufferSlot* slot;

    slot = __LookupLease(manager, lease, &pool);
    if (!slot || !viewOut) {
        return OS_EINVALPARAMS;
    }
    
    if (!__LocallyManaged(slot)) {
        return OS_EBUSY;
    }
    
    *viewOut = (NetBufferView_t) {
        .Data = (uint8_t*)pool->Base + (size_t)lease->Slot * pool->Description.slot_size + manager->DataOffset,
        .Capacity = manager->Capacity,
        .Length = slot->State == SLOT_READY ? slot->Completion.length : 0,
        .Completed = slot->State == SLOT_READY,
        .Status = slot->Completion.status,
        .Detail = slot->Completion.detail
    };
    return OS_EOK;
}

oserr_t
NetBuffersRelease(
    _InOut_ NetBufferManager_t*     manager,
    _In_    const NetBufferLease_t* lease)
{
    struct __BufferPool* pool;
    struct __BufferSlot* slot;

    slot = __LookupLease(manager, lease, &pool);
    if (!slot) {
        return OS_EINVALPARAMS;
    }
    
    if (!__LocallyManaged(slot)) {
        return OS_EBUSY;
    }
    
    // Put a slot back on the free list if it's already managed locally.
    slot->State = SLOT_FREE;
    slot->Next = pool->FreeHead;
    pool->FreeHead = lease->Slot;
    pool->FreeCount++;
    return OS_EOK;
}

static oserr_t
__ValidateSubmissionCapacity(
    _In_ const NetBufferManager_t*       manager,
    _In_ const struct __BufferPool*      pool,
    _In_ enum ctt_netadapter_direction   direction)
{
    if (pool->Outstanding >= manager->OutstandingLimit[__PoolIndex(direction)]) {
        return OS_EBUSY;
    }
    
    if ((uint64_t)manager->Pools[0].Outstanding + manager->Pools[1].Outstanding +
        manager->UnackedCount >= manager->WindowSize) {
        return OS_EBUSY;
    }
    return OS_EOK;
}

oserr_t
NetBuffersPrepare(
    _InOut_ NetBufferManager_t*            manager,
    _In_    const NetBufferLease_t*         lease,
    _In_    uint32_t                        length,
    _Out_   struct ctt_netadapter_packet*  packetOut)
{
    struct __BufferPool* pool;
    struct __BufferSlot* slot;
    oserr_t              status;

    slot = __LookupLease(manager, lease, &pool);
    
    if (!slot || !packetOut) {
        return OS_EINVALPARAMS;
    }
    
    if (length < NET_ADAPTER_ETHERNET_HEADER_SIZE || length > manager->Capacity) {
        return OS_EINVALPARAMS;
    }
    
    if (lease->Direction == CTT_NETADAPTER_DIRECTION_RX && length != manager->Capacity) {
        return OS_EINVALPARAMS;
    }

    if (manager->Closed || pool->State != POOL_REGISTERED) {
        return OS_ENOENT;
    }
    
    if (slot->State != SLOT_HELD || slot->Used) {
        return OS_EBUSY;
    }

    status = __ValidateSubmissionCapacity(manager, pool, lease->Direction);
    if (status != OS_EOK) {
        return status;
    }

    // Reserve the slot and its completion capacity before the caller sends the RPC;
    // from that point, a missing reply cannot prove that the driver did not receive it.
    *packetOut = (struct ctt_netadapter_packet) {
        .id = {
            .queue_id = 0,
            .pool_id = pool->Id,
            .slot_id = lease->Slot,
            .submission_sequence = lease->Sequence
        },
        .data_offset = manager->DataOffset,
        .length = length,
        .flags = 0
    };
    
    slot->State = SLOT_PENDING;
    slot->Used = true;
    slot->AwaitingAdmission = true;
    slot->SubmittedLength = length;
    pool->Outstanding++;
    return OS_EOK;
}

static struct __BufferSlot*
__LookupPacket(
    _In_  NetBufferManager_t*                    manager,
    _In_  const struct ctt_netadapter_packet_id* id,
    _Out_ struct __BufferPool**                  poolOut)
{
    struct __BufferPool* pool;
    struct __BufferSlot* slot;

    if (id->queue_id || !id->pool_id || !id->submission_sequence) {
        return NULL;
    }
    
    // Identify the pool and slot that matches the packet ID
    for (int i = 0; i < __POOL_COUNT; ++i) {
        pool = &manager->Pools[i];
        if (pool->Id != id->pool_id || id->slot_id >= pool->Description.slot_count) {
            continue;
        }
        
        slot = &pool->Slots[id->slot_id];
        if (slot->Sequence != id->submission_sequence) {
            return NULL;
        }
        
        if (!slot->Used || slot->State == SLOT_FREE) {
            return NULL;
        }
        *poolOut = pool;
        return slot;
    }
    return NULL;
}

oserr_t
NetBuffersValidateAdmission(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session,
    _In_ const struct ctt_netadapter_admission* admission)
{
    struct __BufferPool* pool;
    struct __BufferSlot* slot;

    // An admission reply is useful only for this session and when it contains a
    // message to check. Reject unrelated or incomplete replies before looking up a slot.
    if (!__SameSession(manager, session) || !admission) {
        return OS_EINVALPARAMS;
    }
    
    // The reply's status must be one of the OS results this program understands.
    if (admission->status < OS_EOK || admission->status >= __OS_ECOUNT) {
        return OS_EPROTOCOL;
    }
    
    // Do not apply replies after shutdown has started; the session's slots are being
    // retired and must no longer be changed by delayed messages.
    if (manager->Closed) {
        return OS_ENOENT;
    }
    
    // Find the exact submitted slot named by the reply. If it cannot be found, the
    // reply does not match any packet this session currently has in progress.
    slot = __LookupPacket(manager, &admission->id, &pool);
    if (!slot) {
        return OS_EPROTOCOL;
    }
    
    // A repeated reply with the same result is a harmless duplicate. A different
    // result for the same submission conflicts with the reply already accepted.
    if (slot->AdmissionKnown) {
        return slot->AdmissionStatus == admission->status ? OS_EEXISTS : OS_EPROTOCOL;
    }
    
    // Only a slot still waiting for its first admission reply may be updated here.
    // This also rejects an unsolicited reply for a slot that was never submitted.
    if (!slot->AwaitingAdmission) {
        return OS_EPROTOCOL;
    }
    
    // The completion can arrive before the admission reply. In that case the slot is
    // already ready; otherwise it must still be pending or owned by the driver.
    if (slot->State != SLOT_PENDING && slot->State != SLOT_READY) {
        return OS_EPROTOCOL;
    }

    // A completion proves the driver accepted this packet. A later rejection would
    // disagree with that proof, so refuse it and leave the completed slot unchanged.
    if (slot->State == SLOT_READY && admission->status != OS_EOK) {
        return OS_EPROTOCOL;
    }
    return OS_EOK;
}

oserr_t
NetBuffersAdmission(
    _InOut_ NetBufferManager_t*                  manager,
    _In_    const struct ctt_netadapter_session* session,
    _In_    const struct ctt_netadapter_admission* admission,
    _Out_   NetBufferLease_t*                    leaseOut,
    _Out_   bool*                                readyOut)
{
    oserr_t              validation;
    struct __BufferPool* pool;
    struct __BufferSlot* slot;

    if (!leaseOut || !readyOut) {
        return OS_EINVALPARAMS;
    }
    
    validation = NetBuffersValidateAdmission(manager, session, admission);
    if (validation != OS_EOK) {
        return validation;
    }
    
    // We make sure that any validation effort done here does not
    // affect the actual state of the slot.
    slot = __LookupPacket(manager, &admission->id, &pool);
    if (slot->State == SLOT_PENDING) {
        slot->State = admission->status == OS_EOK ? SLOT_DRIVER : SLOT_HELD;
        if (admission->status != OS_EOK) {
            pool->Outstanding--;
        }
    }
    
    slot->AdmissionKnown = true;
    slot->AdmissionStatus = admission->status;
    slot->AwaitingAdmission = false;
    
    *leaseOut = __MakeLease(manager, pool, admission->id.slot_id);
    *readyOut = slot->State == SLOT_READY;
    return OS_EOK;
}

static bool
__SameCompletion(
    _In_ const struct ctt_netadapter_completion* a,
    _In_ const struct ctt_netadapter_completion* b)
{
    // Compare field by field instead of with memcmp: the padding bytes in the struct
    // may differ and are not part of the message.
    if (a->completion_sequence != b->completion_sequence || a->direction != b->direction) {
        return false;
    }
    
    if (a->id.queue_id != b->id.queue_id || a->id.pool_id != b->id.pool_id) {
        return false;
    }
    
    if (a->id.slot_id != b->id.slot_id ||
        a->id.submission_sequence != b->id.submission_sequence) {
        return false;
    }
    return a->status == b->status && a->detail == b->detail && a->length == b->length;
}

static oserr_t
__ValidateCompletionResult(
    _In_ const struct __BufferSlot*             slot,
    _In_ const struct ctt_netadapter_completion* completion)
{
    if (completion->status == CTT_NETADAPTER_COMPLETION_STATUS_SUCCESS) {
        // A successful result must not also carry an error code; OS_EOK means there
        // was no error to report.
        if (completion->detail != OS_EOK) {
            return OS_EPROTOCOL;
        }
        
        // A successful Ethernet frame must include its full header, and it cannot be
        // longer than the data that was submitted in this slot.
        if (completion->length < NET_ADAPTER_ETHERNET_HEADER_SIZE || completion->length > slot->SubmittedLength) {
            return OS_EPROTOCOL;
        }
        
        // A sent frame is either sent in full or reported as a failure. For received
        // frames, a shorter length is valid because the buffer may have spare capacity.
        if (completion->direction == CTT_NETADAPTER_DIRECTION_TX &&
            completion->length != slot->SubmittedLength) {
            return OS_EPROTOCOL;
        }
        return OS_EOK;
    }

    // These are the only supported results that mean the operation did not succeed.
    // Unknown status values could have a different meaning, so do not accept them.
    if (completion->status != CTT_NETADAPTER_COMPLETION_STATUS_ERROR &&
        completion->status != CTT_NETADAPTER_COMPLETION_STATUS_CANCELLED) {
        return OS_EPROTOCOL;
    }
    
    // No frame bytes are valid when the operation fails or is cancelled, and a failed
    // result must include a reason instead of claiming there was no error.
    if (completion->length || completion->detail == OS_EOK) {
        return OS_EPROTOCOL;
    }
    return OS_EOK;
}

static oserr_t
__ValidateCompletionSequence(
    _In_ const NetBufferManager_t*              manager,
    _In_ const struct ctt_netadapter_completion* completion)
{
    const struct __CompletionEntry* entry;
    uint64_t                        sequence = completion->completion_sequence;

    if (sequence <= manager->Processed) {
        return OS_EEXISTS;
    }
    
    if (sequence - manager->Processed > manager->WindowSize) {
        return OS_EBUSY;
    }
    
    entry = &manager->Window[sequence % manager->WindowSize];
    if (entry->Record.completion_sequence) {
        return __SameCompletion(&entry->Record, completion) ? 
            OS_EEXISTS : OS_EPROTOCOL;
    }
    return OS_EOK;
}

oserr_t
NetBuffersValidateCompletion(
    _In_ NetBufferManager_t*                  manager,
    _In_ const struct ctt_netadapter_session* session,
    _In_ const struct ctt_netadapter_completion* completion)
{
    struct __BufferPool* pool;
    struct __BufferSlot* slot;
    oserr_t              status;

    // A completion must belong to this session and include a real completion number.
    // The session check keeps messages from another or older connection out, while
    // zero is reserved to mean "no sequence" in the completion tracking table.
    if (!__SameSession(manager, session) || !completion ||
        !completion->completion_sequence) {
        return OS_EINVALPARAMS;
    }

    // Once the session is closed, its pools and pending work are being retired, so a
    // late driver message must not change their state.
    if (manager->Closed) {
        return OS_ENOENT;
    }

    // The detail field carries an OS error value. Reject values outside the known
    // range so later code never has to interpret an unknown result.
    if (completion->detail < OS_EOK || completion->detail >= __OS_ECOUNT) {
        return OS_EPROTOCOL;
    }

    // Check that this number is still within the range we can track. This also catches
    // old or repeated completions and prevents a far-ahead number from replacing an
    // entry that is still needed for an earlier completion.
    status = __ValidateCompletionSequence(manager, completion);
    if (status != OS_EOK) {
        return status;
    }

    // Match the completion to the exact submitted slot, including its submission
    // number. This prevents a delayed message for an earlier use of a slot from being
    // mistaken for the slot's current use.
    slot = __LookupPacket(manager, &completion->id, &pool);
    if (!slot) {
        return OS_EPROTOCOL;
    }

    // The pool determines whether the slot is for sending or receiving. The message
    // must agree, so a completion for one direction cannot finish the other direction's
    // work by mistake.
    if (completion->direction != pool->Description.direction) {
        return OS_EPROTOCOL;
    }

    // The completion may arrive before or after the separate admission reply, so both
    // pending and driver-owned slots are valid here. Any other state means this slot was
    // not waiting for a completion, or has already completed.
    if (slot->State != SLOT_PENDING && slot->State != SLOT_DRIVER) {
        return OS_EPROTOCOL;
    }

    // Finally, check that the reported status, error detail, and byte count agree with
    // the submitted packet and the rules for this direction.
    return __ValidateCompletionResult(slot, completion);
}

static void
__AdvanceCompletionWindow(
    _InOut_ NetBufferManager_t* manager)
{
    struct __CompletionEntry* entry;
    uint64_t                  next;

    // Move Processed forward only while the next sequence has arrived, stopping at the
    // first gap. We acknowledge up to Processed, so skipping a gap could make the driver
    // discard an earlier RX or TX completion that we have not seen yet.
    while (manager->Processed < UINT64_MAX) {
        next = manager->Processed + 1;
        entry = &manager->Window[next % manager->WindowSize];
        if (entry->Record.completion_sequence != next) {
            break;
        }
        entry->Record.completion_sequence = 0;
        manager->Processed = next;
    }
}

oserr_t
NetBuffersComplete(
    _InOut_ NetBufferManager_t*                  manager,
    _In_    const struct ctt_netadapter_session* session,
    _In_    const struct ctt_netadapter_completion* completion,
    _Out_   NetBufferLease_t*                    leaseOut,
    _Out_   bool*                                readyOut)
{
    oserr_t                   validation;
    struct __BufferPool*      pool;
    struct __BufferSlot*      slot;
    struct __CompletionEntry* entry;
    uint64_t                  sequence;

    if (!leaseOut || !readyOut) {
        return OS_EINVALPARAMS;
    }
    
    // Check the session, sequence number, and submitted slot before changing anything.
    // A completion can arrive more than once or out of order, so accepting only a
    // completion that still matches a live submission keeps an old message from
    // changing a slot that has since been reused.
    validation = NetBuffersValidateCompletion(manager, session, completion);
    if (validation != OS_EOK) {
        return validation;
    }
    
    // Validation has already proved that this packet identifies a slot still owned by
    // the driver. Keep the completion on that slot so the caller can read its result
    // through the lease returned below.
    slot = __LookupPacket(manager, &completion->id, &pool);
    sequence = completion->completion_sequence;
    
    slot->Completion = *completion;
    slot->State = SLOT_READY;

    // The driver no longer has this slot in progress, but its completion still needs
    // acknowledging. Move one item from the in-progress count to the unacknowledged
    // count so both limits continue to describe the work the driver is holding for us.
    pool->Outstanding--;
    manager->UnackedCount++;
    
    // Completions may arrive in a different order from the order they were produced.
    // Keep each one in its sequence-numbered place until every earlier completion has
    // arrived; otherwise acknowledging a later one could make the driver forget a
    // completion that we have not received yet.
    entry = &manager->Window[sequence % manager->WindowSize];
    entry->Record = *completion;
    if (sequence > manager->Highest) {
        // Remember the newest sequence seen, even if an earlier completion is missing.
        manager->Highest = sequence;
    }

    // Advance only through the consecutive completions now present in the window.
    // The helper clears entries as they become part of the complete sequence, leaving
    // any later completion in place while there is still a gap before it.
    __AdvanceCompletionWindow(manager);

    *leaseOut = __MakeLease(manager, pool, completion->id.slot_id);
    // The completion can precede the separate admission reply. In that case the slot
    // is ready internally, but the caller must wait for that reply before using it.
    *readyOut = !slot->AwaitingAdmission;
    return OS_EOK;
}

void
NetBuffersGetStats(
    _In_  const NetBufferManager_t* manager,
    _Out_ NetBufferStats_t*         statsOut)
{
    if (!manager || !statsOut) {
        return;
    }
    
    *statsOut = (NetBufferStats_t) {
        manager->Pools[0].FreeCount,
        manager->Pools[1].FreeCount,
        manager->Pools[0].Outstanding,
        manager->Pools[1].Outstanding,
        manager->UnackedCount,
        manager->Processed, manager->Acknowledged
    };
}

oserr_t
NetBuffersConfirmAcknowledged(
    _InOut_ NetBufferManager_t*                  manager,
    _In_    const struct ctt_netadapter_session* session,
    _In_    uint64_t                             sequence)
{
    if (!__SameSession(manager, session) || sequence > manager->Processed) {
        return OS_EINVALPARAMS;
    }
    
    if (manager->Closed) {
        return OS_ENOENT;
    }
    
    if (sequence > manager->Acknowledged) {
        // Every sequence up to Processed has arrived and was added to UnackedCount
        // exactly once (even if it arrived out of order), so subtracting the range is exact.
        manager->UnackedCount -= (uint32_t)(sequence - manager->Acknowledged);
        manager->Acknowledged = sequence;
    }
    return OS_EOK;
}

static void
__RetirePool(
    _InOut_ struct __BufferPool* pool)
{
    struct __BufferSlot* slot;
    uint32_t             i;

    pool->State = POOL_RETIRED;
    for (i = 0; i < pool->Description.slot_count; ++i) {
        slot = &pool->Slots[i];
        if (slot->State == SLOT_PENDING || slot->State == SLOT_DRIVER) {
            slot->Completion = (struct ctt_netadapter_completion) {
                .status = CTT_NETADAPTER_COMPLETION_STATUS_CANCELLED,
                .detail = OS_ECANCELLED
            };
            slot->State = SLOT_READY;
        }
        slot->AwaitingAdmission = false;
    }
    pool->Outstanding = 0;
}

oserr_t
NetBuffersClosed(
    _InOut_ NetBufferManager_t*                  manager,
    _In_    const struct ctt_netadapter_session* session)
{
    if (!__SameSession(manager, session)) {
        return OS_EINVALPARAMS;
    }
    
    if (manager->Closed) {
        return OS_EOK;
    }
    
    manager->Closed = true;
    manager->UnackedCount = 0;
    
    // Retire all buffer pools to ensure that any 
    // pending slots are properly handled.
    for (int i = 0; i < __POOL_COUNT; ++i) {
        __RetirePool(&manager->Pools[i]);
    }
    return OS_EOK;
}
