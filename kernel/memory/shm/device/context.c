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

#include <assert.h>
#include <heap.h>
#include <limits.h>
#include <debug.h>
#include <string.h>

#include "private.h"

void
SHMDeviceContextInitialize(
    _Out_ struct SHMDeviceContext* context)
{
    // Zeroing also starts the translated bounce counters at zero.
    memset(context, 0, sizeof(struct SHMDeviceContext));
    atomic_init(&context->References, 1);
    MutexConstruct(&context->MappingLock, MUTEX_FLAG_PLAIN);
}

void
SHMDeviceContextAddBounceStorage(
    _In_ struct SHMDeviceContext* context,
    _In_ size_t                   bytes)
{
    struct SHMDeviceBounceUse* use = &context->TranslatedBounce;

    // The held bytes are pool units, and the pool fits in the address space,
    // so the sum cannot overflow.
    MutexLock(&context->MappingLock);
    use->HeldBytes += bytes;
    if (use->HeldBytes > use->PeakBytes) {
        use->PeakBytes = use->HeldBytes;
    }
    MutexUnlock(&context->MappingLock);
}

void
SHMDeviceContextRemoveBounceStorage(
    _In_ struct SHMDeviceContext* context,
    _In_ size_t                   bytes)
{
    MutexLock(&context->MappingLock);
    assert(context->TranslatedBounce.HeldBytes >= bytes);
    context->TranslatedBounce.HeldBytes -= bytes;
    MutexUnlock(&context->MappingLock);
}

// Add one to a translated bounce counter of the context under MappingLock.
// Returns true when the counter was zero before, so the caller can log the
// first event at warning level and only trace the later ones.
static bool
__CountBounceEvent(
    _In_    struct SHMDeviceContext* context,
    _InOut_ uint64_t*                counter)
{
    bool first;

    MutexLock(&context->MappingLock);
    first = *counter == 0;
    (*counter)++;
    MutexUnlock(&context->MappingLock);
    return first;
}

bool
SHMDeviceContextCountBounceMapping(
    _In_ struct SHMDeviceContext* context)
{
    return __CountBounceEvent(context, &context->TranslatedBounce.Mappings);
}

bool
SHMDeviceContextCountBounceLimitRefusal(
    _In_ struct SHMDeviceContext* context)
{
    return __CountBounceEvent(context, &context->TranslatedBounce.LimitRefusals);
}

bool
SHMDeviceContextCountBouncePoolShortage(
    _In_ struct SHMDeviceContext* context)
{
    return __CountBounceEvent(context, &context->TranslatedBounce.PoolShortages);
}

// Log the translated bounce totals of a context that is being freed. No other
// reference exists any more, so the counters are read without MappingLock.
static void
__ReportBounceUse(
    _In_ const struct SHMDeviceContext* context)
{
    const struct SHMDeviceBounceUse* use = &context->TranslatedBounce;

    // The first translated bounce mapping was reported when it was prepared.
    // Report the total once more, so the cost of a driver's copies can be
    // read from the log without tracing every mapping.
    if (use->Mappings != 0) {
        WARNING("SHMDeviceContextRelease: context %p prepared %llu translated bounce mappings, holding at most %zu bytes",
                context, (unsigned long long)use->Mappings, use->PeakBytes);
    }

    // Only the first refusal and the first shortage were logged as warnings,
    // the later ones were traced. Give the totals here.
    if (use->LimitRefusals != 0 || use->PoolShortages != 0) {
        WARNING("SHMDeviceContextRelease: context %p had %llu translated bounce requests above the size limit and %llu pool shortages",
                context, (unsigned long long)use->LimitRefusals, (unsigned long long)use->PoolShortages);
    }

    // Held bytes left at this point belong to units that failed to go back to
    // the pool after a failed preparation. They were kept out of the pool on
    // purpose and are now lost for good. An installed mapping cannot reach
    // here with storage, because it keeps a context reference until its
    // storage is released.
    if (use->HeldBytes != 0) {
        ERROR("SHMDeviceContextRelease: context %p is freed with %zu bytes of translated bounce storage that never went back to the pool",
              context, use->HeldBytes);
    }
}

oserr_t
SHMDeviceContextAcquire(
    _In_ struct SHMDeviceContext* context)
{
    unsigned int references;

    // The caller must hold a reference until this call returns, so the context
    // cannot be freed in between. Atomic updates let several threads add
    // references at the same time.
    if (context == NULL) {
        WARNING("SHMDeviceContextAcquire: called with a NULL context");
        return OS_EINVALPARAMS;
    }

    references = atomic_load(&context->References);
    do {
        // Refuse to count past the maximum. A wrapped count could free the
        // context while it is still in use.
        if (references == 0) {
            return OS_ECANCELLED;
        }

        if (references == UINT_MAX) {
            WARNING("SHMDeviceContextAcquire: context %p reference count is at its maximum", context);
            return OS_EOVERFLOW;
        }
    } while (!atomic_compare_exchange_weak(&context->References, &references, references + 1));
    return OS_EOK;
}

void
SHMDeviceContextRelease(
    _InOut_ struct SHMDeviceContext** context)
{
    struct SHMDeviceContext* owned;

    // Clear the caller's pointer even when other references remain, so running
    // cleanup again with this pointer cannot drop the count a second time.
    if (context == NULL || *context == NULL) {
        return;
    }

    owned = *context;
    *context = NULL;
    
    if (atomic_fetch_sub(&owned->References, 1) == 1) {
        __ReportBounceUse(owned);

        // A failed preparation can reach the last release before a binding was
        // attached.
        TRACE("SHMDeviceContextRelease: context %p freed", owned);
        DMABindingDetachContext(&owned->Binding, owned);
        DMAAddressSpaceRelease(&owned->AddressSpace);
        MutexDestruct(&owned->MappingLock);
        kfree(owned);
    }
}

oserr_t
SHMDeviceContextClose(
    _InOut_ struct SHMDeviceContext** context)
{
    bool hasMappings;

    // Callers make sure close does not run at the same time as other context
    // operations. A recovery pass may still hold temporary references, so the
    // reference count cannot tell when to stop. Close can proceed only when no
    // mappings remain on the list.
    MutexLock(&(*context)->MappingLock);
    hasMappings = (*context)->Mappings != NULL;
    MutexUnlock(&(*context)->MappingLock);
    if (hasMappings) {
        return OS_EBUSY;
    }

    // The binding stays attached until the last reference is dropped,
    // including temporary ones held by a recovery pass. The driver cannot open
    // another context on this binding until then.
    SHMDeviceContextRelease(context);
    return OS_EOK;
}
