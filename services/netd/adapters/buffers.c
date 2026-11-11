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
#include <internal/_shm_transfer.h>
#include <ddk/convert/grants.h>
#include <os/memory.h>
#include <os/mollenos.h>
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
 * @brief Current use and completion data for one shared-memory slot.
 */
struct __BufferSlot {
    enum __SlotState State;
    uint32_t         Next;
    uint64_t         Sequence;
    uint32_t         SubmittedLength;

    // Set before submission. A rejected lease cannot be submitted again.
    bool Used;

    // Waiting for the admission reply. The completion may arrive first.
    bool    AwaitingAdmission;
    bool    AdmissionKnown;
    oserr_t AdmissionStatus;

    // Completion from the driver, may arrive before or after the admission reply.
    struct ctt_netadapter_completion Completion;
};

/**
 * @brief Shared memory and slot state for one transfer direction.
 */
struct __BufferPool {
    // The handle to the shared memory region for this pool
    OSHandle_t   Handle;
    GrantToken_t Token;
    void*        Base;

    struct ctt_netadapter_pool Description;

    struct __BufferSlot* Slots;
    enum __PoolState     State;

    uint32_t             Id;
    uint32_t             FreeHead;
    uint32_t             FreeCount;

    // PENDING plus DRIVER slots. If a completion arrives before its admission
    // reply, only the completion decreases this count.
    uint32_t Outstanding;
};

/**
 * @brief Stores a completion until all earlier completion sequences are present.
 */
struct __CompletionEntry {
    // Sequence 0 marks an empty entry. The table has max_unacked_completions
    // entries, indexed by sequence, so later completions can wait for missing ones.
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

    // Completions we have received but not acknowledged. The driver must
    // keep space for them until we acknowledge them.
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

    // Both the ID and generation must match.
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

// Returns true if the amount fits and has been added to the total.
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
        if (manager->Pools[i].Token.Version != 0) {
            OSGrantTokenRevoke(manager->Pools[i].Token.ControlHandleID);
        }

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
__GetSlotAlignment(
    _In_  size_t  deviceAlignment,
    _In_  size_t  pageSize,
    _Out_ size_t* alignmentOut)
{
    OSSystemCPUInfo_t info;
    size_t            queried;
    size_t            line;
    oserr_t           status;

    status = OSSystemQuery(OSSYSTEMQUERY_CPUINFO, &info, sizeof(info), &queried);
    if (status != OS_EOK) {
        return status;
    } else if (queried != sizeof(OSSystemCPUInfo_t)) {
        return OS_EINVALPARAMS;
    }

    line = info.DataCacheLineSizeBytes;
    if (line == 0 || (line & (line - 1)) != 0 || line > pageSize) {
        return OS_EINVALPARAMS;
    }

    *alignmentOut = line > deviceAlignment ? line : deviceAlignment;
    return OS_EOK;
}

static oserr_t
__CalculateBufferLayout(
    _In_  const struct ctt_netadapter_info* info,
    _In_  const NetBufferConfig_t*          config,
    _In_  size_t                            pageSize,
    _In_  size_t                            slotAlignment,
    _Out_ struct __BufferLayout*            layout)
{
    uint64_t offset, stride, total = sizeof(NetBufferManager_t);
    uint64_t slotBytes, windowBytes;
    bool     valid;
    int      i;

    // Leave headroom before the frame and align the data offset for the device.
    // SHM is page-aligned, so this works for alignments up to one page. The driver
    // checks any other memory requirements.
    valid = __RoundUpAligned(info->required_headroom, info->buffer_alignment, &offset);
    if (!valid) {
        return OS_EOVERFLOW;
    }
    
    // SHM reserves whole cache lines during DMA, including on coherent devices.
    // Pad each slot so a receive cannot conflict with the adjacent slot's access.
    valid = __RoundUpAligned(
        offset + config->FrameCapacity + info->required_tailroom,
        slotAlignment,
        &stride
    );
    if (!valid) {
        return OS_EOVERFLOW;
    }
    
    // The slot size must fit in the protocol's 32-bit field.
    if (stride > UINT32_MAX) {
        return OS_EOVERFLOW;
    }

    // Check both pool sizes before allocating memory, so a request that exceeds
    // the limits needs no cleanup.
    layout->SlotCounts[0] = config->TxSlots;
    layout->SlotCounts[1] = config->RxSlots;
    for (i = 0; i < __POOL_COUNT; ++i) {
        // The device advertises a maximum slot count for each pool separately.
        if (layout->SlotCounts[i] > info->max_slots_per_pool) {
            return OS_EOVERFLOW;
        }

        // SHM requires whole pages.
        if (!__RoundUpAligned(stride * layout->SlotCounts[i], pageSize, &layout->PoolBytes[i])) {
            return OS_EOVERFLOW;
        }

        // The pool must fit in size_t and within the device's per-pool limit.
        if (layout->PoolBytes[i] > SIZE_MAX || layout->PoolBytes[i] > info->max_pool_bytes) {
            return OS_EOVERFLOW;
        }

        // Count the local slot records in the memory budget too.
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
    
    // The combined TX and RX pool size must also fit the device's limit.
    if (layout->PoolBytes[0] > info->max_registered_bytes) {
        return OS_EOVERFLOW;
    }
    
    if (layout->PoolBytes[1] > info->max_registered_bytes - layout->PoolBytes[0]) {
        return OS_EOVERFLOW;
    }

    // Each unacknowledged completion needs a local record. Count these against
    // our memory budget, separately from the device's shared-memory limit.
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
    _InOut_ struct __BufferPool*              pool,
    _In_    const struct ctt_netadapter_info* info,
    _In_    const struct __BufferLayout*      layout,
    _In_    int                               index)
{
    SHMTransfer_t transfer;
    uint32_t      slot;
    uint32_t      count = layout->SlotCounts[index];
    uint64_t      bytes = layout->PoolBytes[index];
    oserr_t       status;

    pool->Slots = calloc(count, sizeof(*pool->Slots));
    if (!pool->Slots) {
        return OS_EOOM;
    }

    status = SHMCreate(
        &(SHM_t) {
            .Flags = SHM_COMMIT | SHM_CLEAN,
            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
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
        .slot_size = layout->SlotSize,
        .slot_count = count
    };
    
    status = __SHMTransferCreate(
        pool->Handle.ID,
        0,
        (size_t)bytes,
        index == 0 ? GrantTokenScopeRead : GrantTokenScopeRead | GrantTokenScopeWrite,
        &transfer
    );
    if (status != OS_EOK) {
        return status;
    }

    pool->Token = transfer.Token;
    status = __ToSysSHMTransfer(&transfer, &pool->Description.buffer);
    if (status != OS_EOK) {
        return status;
    }

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
    size_t                slotAlignment;
    NetBufferManager_t*   manager;
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

    status = __GetSlotAlignment(info->buffer_alignment, pageSize, &slotAlignment);
    if (status != OS_EOK) {
        return status;
    }

    status = __CalculateBufferLayout(info, config, pageSize, slotAlignment, &layout);
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
    
    // Leave room for one TX completion. If RX uses every completion entry, we
    // cannot send the ARP packets needed to get traffic flowing.
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
    
    // Mark registration before the RPC. If the reply is lost, the driver may
    // already be using the shared memory.
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

    // Reserve the slot and completion space before sending the RPC, because the
    // driver may accept the packet even if we never receive the reply to it.
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

    // The reply must be present and belong to this session.
    if (!__SameSession(manager, session) || !admission) {
        return OS_EINVALPARAMS;
    }
    
    // Only accept known OS results.
    if (admission->status < OS_EOK || admission->status >= __OS_ECOUNT) {
        return OS_EPROTOCOL;
    }
    
    // After shutdown starts, delayed replies must leave the slots unchanged.
    if (manager->Closed) {
        return OS_ENOENT;
    }
    
    // Match the reply to the submitted slot.
    slot = __LookupPacket(manager, &admission->id, &pool);
    if (!slot) {
        return OS_EPROTOCOL;
    }
    
    // Repeated replies must agree with the first result.
    if (slot->AdmissionKnown) {
        return slot->AdmissionStatus == admission->status ? OS_EEXISTS : OS_EPROTOCOL;
    }
    
    // The slot must still be waiting for its first admission reply.
    if (!slot->AwaitingAdmission) {
        return OS_EPROTOCOL;
    }
    
    // A completion can arrive first and make the slot READY. Otherwise, it must
    // still be PENDING.
    if (slot->State != SLOT_PENDING && slot->State != SLOT_READY) {
        return OS_EPROTOCOL;
    }

    // A completed packet was accepted. Reject a conflicting admission reply.
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
    
    // Validation must finish before we change the slot.
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
    // Compare fields separately. Struct padding may differ between messages.
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
        // Success requires OS_EOK.
        if (completion->detail != OS_EOK) {
            return OS_EPROTOCOL;
        }
        
        // The frame must include an Ethernet header and fit in the submitted range.
        if (completion->length < NET_ADAPTER_ETHERNET_HEADER_SIZE || completion->length > slot->SubmittedLength) {
            return OS_EPROTOCOL;
        }
        
        // TX must send the whole frame. An RX frame may use less than the buffer.
        if (completion->direction == CTT_NETADAPTER_DIRECTION_TX &&
            completion->length != slot->SubmittedLength) {
            return OS_EPROTOCOL;
        }
        return OS_EOK;
    }

    // The remaining supported results are ERROR and CANCELLED.
    if (completion->status != CTT_NETADAPTER_COMPLETION_STATUS_ERROR &&
        completion->status != CTT_NETADAPTER_COMPLETION_STATUS_CANCELLED) {
        return OS_EPROTOCOL;
    }
    
    // Failed or cancelled transfers have no valid bytes and must report an error.
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

    // Require the current session and a nonzero sequence. Sequence 0 marks an
    // empty completion entry.
    if (!__SameSession(manager, session) || !completion ||
        !completion->completion_sequence) {
        return OS_EINVALPARAMS;
    }

    // Late completions must leave a closed session unchanged.
    if (manager->Closed) {
        return OS_ENOENT;
    }

    // The detail must be a known OS result.
    if (completion->detail < OS_EOK || completion->detail >= __OS_ECOUNT) {
        return OS_EPROTOCOL;
    }

    // Check for duplicates and sequences outside the window before writing an
    // entry, so a later completion cannot overwrite one that we still need to keep.
    status = __ValidateCompletionSequence(manager, completion);
    if (status != OS_EOK) {
        return status;
    }

    // The submission number must match too. A delayed completion must not finish
    // a newer packet that happens to use the same slot.
    slot = __LookupPacket(manager, &completion->id, &pool);
    if (!slot) {
        return OS_EPROTOCOL;
    }

    // The direction must match the pool.
    if (completion->direction != pool->Description.direction) {
        return OS_EPROTOCOL;
    }

    // Accept PENDING and DRIVER slots because the completion may arrive before
    // the admission reply.
    if (slot->State != SLOT_PENDING && slot->State != SLOT_DRIVER) {
        return OS_EPROTOCOL;
    }

    // Check the result and byte count against the submitted packet.
    return __ValidateCompletionResult(slot, completion);
}

static void
__AdvanceCompletionWindow(
    _InOut_ NetBufferManager_t* manager)
{
    struct __CompletionEntry* entry;
    uint64_t                  next;

    // Stop at the first missing sequence. We acknowledge everything up to
    // Processed, so skipping a gap would let the driver discard a result we
    // have not received.
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
    
    // Validate before changing the slot. Repeated or delayed completions must
    // leave a newer submission unchanged.
    validation = NetBuffersValidateCompletion(manager, session, completion);
    if (validation != OS_EOK) {
        return validation;
    }
    
    // Validation found a submitted slot. Save the result there for the caller
    // to read through the returned lease.
    slot = __LookupPacket(manager, &completion->id, &pool);
    sequence = completion->completion_sequence;
    
    slot->Completion = *completion;
    slot->State = SLOT_READY;

    // The packet is finished. Its completion still needs an acknowledgement.
    pool->Outstanding--;
    manager->UnackedCount++;
    
    // Keep out-of-order completions in the window until the earlier ones arrive.
    entry = &manager->Window[sequence % manager->WindowSize];
    entry->Record = *completion;
    if (sequence > manager->Highest) {
        // Remember the newest sequence seen, even if an earlier completion is missing.
        manager->Highest = sequence;
    }

    // Clear consecutive entries and leave those beyond a gap in the window.
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
    
    // Cancel pending packets before making the closed pools available for cleanup.
    for (int i = 0; i < __POOL_COUNT; ++i) {
        __RetirePool(&manager->Pools[i]);
    }
    return OS_EOK;
}
