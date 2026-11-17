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

#include <heap.h>
#include <limits.h>
#include <debug.h>
#include <string.h>

#include "private.h"

void
SHMDeviceContextInitialize(
    _Out_ struct SHMDeviceContext* context)
{
    memset(context, 0, sizeof(struct SHMDeviceContext));
    atomic_init(&context->References, 1);
    MutexConstruct(&context->MappingLock, MUTEX_FLAG_PLAIN);
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
