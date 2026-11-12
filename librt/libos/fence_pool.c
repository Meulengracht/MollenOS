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
 */

//#define __TRACE

#include <ddk/utils.h>
#include <os/fence_pool.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "fence_private.h"

#define FENCE_POOL_VERSION 1U
#define FENCE_POOL_BITS    (sizeof(unsigned int) * CHAR_BIT)

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Shared pools require lock-free integer atomics");

/**
 * @brief Pool layout header at the start of shared storage.
 *
 * Memory layout: header, atomic free-bit words, alignment padding, Capacity slots.
 * A set bit marks an available slot. Bits beyond Capacity must stay clear.
 * Version, ID and Capacity are fixed after initialization. Local bindings use
 * this header to calculate addresses, so all processes must use the same ABI.
 * All users must finish before storage is replaced or initialized again.
 */
struct __OSFencePoolHeader {
    unsigned int Version;
    unsigned int ID;
    unsigned int Capacity;
};

/**
 * @brief Shared slot with its reference count, generation and completion state.
 *
 * References includes producers, consumers, reservation entries and dependencies
 * in all processes. Generation changes when the last reference is released and
 * a new producer takes the slot. TryTake increases it before returning a reference.
 * UINT_MAX is the last usable generation. The slot is then removed from use.
 *
 * Completion uses the states in fence_private.h. The producer must keep a
 * reference until notification finishes, as consumers may release all their
 * references as soon as they see Complete.
 */
struct __OSFencePoolSlot {
    atomic_uint           References;
    atomic_uint           Generation;
    OSFenceStorage_t Completion;
};

static size_t
__PoolWords(
    _In_ unsigned int capacity)
{
    // Divide before rounding up. Adding FENCE_POOL_BITS - 1 to capacity first
    // could overflow for values near UINT_MAX.
    return capacity / FENCE_POOL_BITS + (capacity % FENCE_POOL_BITS != 0);
}

static size_t
__PoolSlotsOffset(
    _In_ unsigned int capacity)
{
    size_t offset;
    size_t alignment = _Alignof(struct __OSFencePoolSlot);

    // Free bits use whole atomic words. Pad the slot array to keep its atomics
    // and completion storage naturally aligned.
    offset = sizeof(struct __OSFencePoolHeader) + __PoolWords(capacity) * sizeof(atomic_uint);
    return (offset + alignment - 1) & ~(alignment - 1);
}

size_t
OSFencePoolSize(
    _In_ unsigned int capacity)
{
    size_t offset;

    if (capacity == 0) {
        return 0;
    }
    
    offset = __PoolSlotsOffset(capacity);
    if (capacity > (SIZE_MAX - offset) / sizeof(struct __OSFencePoolSlot)) {
        return 0;
    }
    return offset + capacity * sizeof(struct __OSFencePoolSlot);
}

static bool
__PoolStorageValid(
    _In_    void*  storage,
    _In_    size_t size)
{
    return storage != NULL && (uintptr_t)storage % _Alignof(max_align_t) == 0 &&
        size >= sizeof(struct __OSFencePoolHeader);
}

oserr_t
OSFencePoolInitialize(
    _Out_ void*        storage,
    _In_  size_t       size,
    _In_  unsigned int poolID,
    _In_  unsigned int capacity)
{
    struct __OSFencePoolHeader* header = storage;
    struct __OSFencePoolSlot*   slots;
    atomic_uint*                available;
    size_t                      required = OSFencePoolSize(capacity);

    if (!__PoolStorageValid(storage, size) || required == 0 || required > size) {
        WARNING("OSFencePoolInitialize: invalid storage %p, size %zu or capacity %u for pool %u",
            storage, size, capacity, poolID);
        return OS_EINVALPARAMS;
    }

    header->Version = FENCE_POOL_VERSION;
    header->ID = poolID;
    header->Capacity = capacity;

    available = (atomic_uint*)(header + 1);
    slots = (void*)((char*)storage + __PoolSlotsOffset(capacity));

    for (size_t i = 0; i < __PoolWords(capacity); i++) {
        unsigned int remaining = capacity - (unsigned int)(i * FENCE_POOL_BITS);
        // Unused bits in the last word must stay clear. TryTake uses the bit
        // index directly without another capacity check.
        atomic_init(&available[i], remaining >= FENCE_POOL_BITS ? UINT_MAX : (1U << remaining) - 1);
    }

    for (size_t i = 0; i < capacity; i++) {
        atomic_init(&slots[i].References, 0);
        atomic_init(&slots[i].Generation, 0);
        __OSFenceStorageInitialize(&slots[i].Completion);
    }

    TRACE("OSFencePoolInitialize: pool %u, capacity %u, storage %p",
        poolID, capacity, storage);
    return OS_EOK;
}

oserr_t
OSFencePoolBind(
    _In_  void*          storage,
    _In_  size_t         size,
    _Out_ OSFencePool_t* poolOut)
{
    struct __OSFencePoolHeader* header = storage;
    size_t                      required;

    if (poolOut == NULL || !__PoolStorageValid(storage, size)) {
        WARNING("OSFencePoolBind: invalid storage %p, size %zu or missing output", storage, size);
        return OS_EINVALPARAMS;
    }

    required = OSFencePoolSize(header->Capacity);
    if (header->Version != FENCE_POOL_VERSION || required == 0 || required > size) {
        WARNING("OSFencePoolBind: invalid pool header (version %u, capacity %u, size %zu)",
            header->Version, header->Capacity, size);
        return OS_EINVALPARAMS;
    }

    poolOut->ID = header->ID;
    poolOut->Capacity = header->Capacity;
    poolOut->Available = (atomic_uint*)(header + 1);
    poolOut->Slots = (void*)((char*)storage + __PoolSlotsOffset(header->Capacity));
    return OS_EOK;
}

oserr_t
OSFencePoolTryTake(
    _In_    const OSFencePool_t* pool,
    _InOut_ OSFenceReference_t*  referenceOut)
{
    if (pool == NULL || referenceOut == NULL) {
        WARNING("OSFencePoolTryTake: missing pool or reference output");
        return OS_EINVALPARAMS;
    }

    if (referenceOut->Generation != 0) {
        TRACE("OSFencePoolTryTake: output is not empty");
        return OS_EBUSY;
    }

    // Check each word once and each set bit at most once. If another producer
    // takes a slot, continue the scan. Slots freed after their word was checked
    // may be found on retry.
    for (size_t word = 0; word < __PoolWords(pool->Capacity); word++) {
        unsigned int              bits;

        bits = atomic_load_explicit(&pool->Available[word], memory_order_relaxed);
        while (bits != 0) {
            struct __OSFencePoolSlot* slot;
            unsigned int              index;
            unsigned int              bit;
            unsigned int              previous;
            unsigned int              generation;

            bit = (unsigned int)__builtin_ctz(bits);
            bits &= ~(1U << bit);
            
            // Only the producer clearing a set bit may initialize this slot.
            // The acquire operation pairs with the last release's free-bit store,
            // which follows all previous accesses to the completion storage.
            previous = atomic_fetch_and_explicit(&pool->Available[word], ~(1U << bit), memory_order_acquire);
            if (!(previous & (1U << bit))) {
                continue;
            }
            
            index = (unsigned int)(word * FENCE_POOL_BITS) + bit;
            slot = &pool->Slots[index];
            generation = atomic_load_explicit(&slot->Generation, memory_order_relaxed);
            
            if (generation == UINT_MAX) {
                // Keep the bit clear. Wrapping the generation would let an old
                // reference match a new completion.
                WARNING("OSFencePoolTryTake: pool %u slot %u exhausted its generations", pool->ID, index);
                continue;
            }
            
            __OSFenceStorageInitialize(&slot->Completion);
            atomic_store(&slot->Completion.State, __OSFenceBinding);
            atomic_store_explicit(&slot->References, 1, memory_order_relaxed);
           
            // Initialize the slot before another process can validate Generation.
            // The caller must still synchronize transfer of the reference to peers.
            atomic_store_explicit(&slot->Generation, generation + 1, memory_order_release);
            
            *referenceOut = (OSFenceReference_t) {
                pool->ID,
                index,
                generation + 1
            };
            TRACE("OSFencePoolTryTake: pool %u slot %u generation %u", pool->ID, index, generation + 1);
            return OS_EOK;
        }
    }

    TRACE("OSFencePoolTryTake: pool %u has no available slot", pool->ID);
    return OS_EBUSY;
}

static oserr_t
__PoolSlot(
    _In_  const OSFencePool_t*       pool,
    _In_  const OSFenceReference_t*  reference,
    _Out_ struct __OSFencePoolSlot** slotOut)
{
    struct __OSFencePoolSlot* slot;

    // The caller must hold a counted reference and prevent its concurrent release.
    // This check adds no reference. Without one, the slot could be reused between
    // checking Generation and accessing the completion.
    if (pool == NULL || reference == NULL || reference->Generation == 0) {
        TRACE("__PoolSlot: missing pool or empty reference");
        return OS_EINVALPARAMS;
    }

    if (reference->Pool != pool->ID || reference->Slot >= pool->Capacity) {
        TRACE("__PoolSlot: reference pool %u slot %u does not match pool %u capacity %u",
            reference->Pool, reference->Slot, pool->ID, pool->Capacity);
        return OS_EINVALPARAMS;
    }

    slot = &pool->Slots[reference->Slot];
    if (atomic_load_explicit(&slot->Generation, memory_order_acquire) != reference->Generation ||
        atomic_load_explicit(&slot->References, memory_order_relaxed) == 0) {
        TRACE("__PoolSlot: pool %u slot %u generation %u is obsolete",
            reference->Pool, reference->Slot, reference->Generation);
        return OS_ENOENT;
    }

    *slotOut = slot;
    return OS_EOK;
}

oserr_t
OSFencePoolAcquire(
    _In_ const OSFencePool_t*      pool,
    _In_ const OSFenceReference_t* reference)
{
    struct __OSFencePoolSlot* slot;
    unsigned int              references;
    oserr_t                   status;

    status = __PoolSlot(pool, reference, &slot);
    if (status != OS_EOK) {
        return status;
    }

    // The existing reference keeps the count above zero. Loading Generation
    // already synchronized with initialization, so the count can use relaxed atomics.
    references = atomic_load_explicit(&slot->References, memory_order_relaxed);
    do {
        if (references == UINT_MAX) {
            ERROR("OSFencePoolAcquire: pool %u slot %u generation %u reference count overflow",
                reference->Pool, reference->Slot, reference->Generation);
            return OS_EOVERFLOW;
        }
    } while (!atomic_compare_exchange_weak_explicit(&slot->References, &references,
        references + 1, memory_order_relaxed, memory_order_relaxed));
    return OS_EOK;
}

oserr_t
OSFencePoolRelease(
    _In_    const OSFencePool_t* pool,
    _InOut_ OSFenceReference_t*  reference)
{
    struct __OSFencePoolSlot* slot;
    unsigned int              references;
    oserr_t                   status;

    if (reference == NULL || reference->Generation == 0) {
        return OS_EOK;
    }

    status = __PoolSlot(pool, reference, &slot);
    if (status != OS_EOK) {
        return status;
    }

    references = atomic_load_explicit(&slot->References, memory_order_relaxed);
    do {
        if (references == 1 && atomic_load(&slot->Completion.State) == __OSFencePending) {
            // The producer must signal a result before the slot can be reused,
            // also when work was cancelled.
            TRACE("OSFencePoolRelease: pool %u slot %u generation %u is still pending",
                reference->Pool, reference->Slot, reference->Generation);
            return OS_EBUSY;
        }
    } while (!atomic_compare_exchange_weak_explicit(&slot->References, &references,
        references - 1, memory_order_acq_rel, memory_order_relaxed));
    
    if (references == 1) {
        // All previous calls, including wake, must have finished before the last
        // reference is released. The next producer synchronizes on this free bit.
        // Complete alone does not tell us that signal has finished using storage.
        atomic_fetch_or_explicit(&pool->Available[reference->Slot / FENCE_POOL_BITS],
            1U << (reference->Slot % FENCE_POOL_BITS), memory_order_release);
    }

    memset(reference, 0, sizeof(OSFenceReference_t));
    return OS_EOK;
}

oserr_t
OSFencePoolSubmit(
    _In_ const OSFencePool_t*      pool,
    _In_ const OSFenceReference_t* reference)
{
    struct __OSFencePoolSlot* slot;
    int                       state = __OSFenceBinding;
    oserr_t                   status;

    status = __PoolSlot(pool, reference, &slot);
    if (status != OS_EOK) {
        return status;
    }

    // TryTake left the slot in Binding. Consumers can wait once it is Pending.
    // If submission was rejected, release the slot while it is still in Binding.
    if (!atomic_compare_exchange_strong(&slot->Completion.State, &state, __OSFencePending)) {
        TRACE("OSFencePoolSubmit: pool %u slot %u generation %u is not prepared (state %d)",
            reference->Pool, reference->Slot, reference->Generation, state);
        return OS_EBUSY;
    }
    return OS_EOK;
}

oserr_t
OSFencePoolPoll(
    _In_  const OSFencePool_t*      pool,
    _In_  const OSFenceReference_t* reference,
    _Out_ oserr_t*                  resultOut)
{
    struct __OSFencePoolSlot* slot;
    oserr_t                   status;

    if (resultOut == NULL) {
        TRACE("OSFencePoolPoll: missing result output");
        return OS_EINVALPARAMS;
    }

    status = __PoolSlot(pool, reference, &slot);
    if (status != OS_EOK) {
        return status;
    }
    return __OSFenceStoragePoll(&slot->Completion, resultOut);
}

oserr_t
OSFencePoolWait(
    _In_  const OSFencePool_t*      pool,
    _In_  const OSFenceReference_t* reference,
    _In_  const OSTimestamp_t*      deadline,
    _Out_ oserr_t*                  resultOut)
{
    struct __OSFencePoolSlot* slot;
    oserr_t                   status;

    if (resultOut == NULL) {
        WARNING("OSFencePoolWait: missing result output");
        return OS_EINVALPARAMS;
    }

    status = __PoolSlot(pool, reference, &slot);
    if (status != OS_EOK) {
        return status;
    }
    return __OSFenceStorageWait(
        &slot->Completion,
        0,
        deadline,
        resultOut
    );
}

oserr_t
OSFencePoolSignal(
    _In_ const OSFencePool_t*      pool,
    _In_ const OSFenceReference_t* reference,
    _In_ oserr_t                   result)
{
    struct __OSFencePoolSlot* slot;
    oserr_t                   status;

    status = __PoolSlot(pool, reference, &slot);
    if (status != OS_EOK) {
        return status;
    }
    return __OSFenceStorageSignal(&slot->Completion, 0, result);
}
