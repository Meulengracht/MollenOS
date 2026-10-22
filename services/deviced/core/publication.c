/**
 * Copyright 2026, Philip Meulengracht
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
 * Registers related devices before allowing their drivers to start.
 */

//#define __TRACE

#include <core/publication.h>
#include <ddk/utils.h>
#include <devices.h>
#include <stdlib.h>

// Tracks a registered ID and whether binding remains pending across retries.
struct __DmPublicationEntry {
    struct __DmPublicationEntry* Previous;
    struct __DmPublicationEntry* Next;
    uuid_t*                      DeviceId;
    int                          BindingPending;
};

oserr_t
DmPublicationAdd(
    _InOut_ struct DmPublicationGroup*         group,
    _In_    const struct DmDeviceRegistration* registration,
    _In_    int                               allowBinding,
    _InOut_ uuid_t*                           deviceId)
{
    struct __DmPublicationEntry* entry;
    oserr_t                     status;
    TRACE("DmPublicationAdd(group=%p, state=%i, binding=%i)", group, group->State, allowBinding);

    // A finished set may already have drivers using it. Do not extend that set
    // or replace an ID that still belongs to a registered device.
    if (group->State != DmPublicationCollecting || *deviceId != UUID_INVALID) {
        WARNING("DmPublicationAdd: group %p cannot accept device %u in state %i",
                group, *deviceId, group->State);
        return OS_EBUSY;
    }

    // Reserve bookkeeping before transferring the description to the registry.
    // Otherwise an allocation failure could leave an entry we cannot remove.
    entry = calloc(1, sizeof(struct __DmPublicationEntry));
    if (entry == NULL) {
        ERROR("DmPublicationAdd: failed to allocate bookkeeping for group %p", group);
        return OS_EOOM;
    }

    status = DmDeviceCreateWithProvider(registration, 0, deviceId);
    if (status != OS_EOK) {
        WARNING("DmPublicationAdd: device registration for group %p failed: %u", group, status);
        // Failed registration leaves the description with the caller.
        free(entry);
        return status;
    }

    entry->DeviceId = deviceId;
    entry->BindingPending = allowBinding;
    entry->Previous = group->Last;
    
    // The first entry has no predecessor whose next pointer can be updated.
    if (group->Last == NULL) {
        group->First = entry;
    } else {
        group->Last->Next = entry;
    }
    group->Last = entry;

    TRACE("DmPublicationAdd: registered device %u in group %p", *deviceId, group);
    return OS_EOK;
}

oserr_t
DmPublicationFinish(
    _InOut_ struct DmPublicationGroup* group)
{
    TRACE("DmPublicationFinish(group=%p, state=%i)", group, group->State);
    // Finishing must not reopen a group that is binding or being removed.
    if (group->State != DmPublicationCollecting) {
        WARNING("DmPublicationFinish: group %p cannot finish in state %i", group, group->State);
        return OS_EBUSY;
    }
    group->State = DmPublicationReady;
    return OS_EOK;
}

oserr_t
DmPublicationEnableBinding(
    _InOut_ struct DmPublicationGroup* group)
{
    struct __DmPublicationEntry* entry;
    oserr_t                     status;
    TRACE("DmPublicationEnableBinding(group=%p, state=%i)", group, group->State);

    // Drivers need the complete set, and must never start during removal.
    if (group->State != DmPublicationReady && group->State != DmPublicationBinding) {
        WARNING("DmPublicationEnableBinding: group %p is not ready in state %i",
                group, group->State);
        return OS_EBUSY;
    }
    group->State = DmPublicationBinding;

    for (entry = group->First; entry != NULL; entry = entry->Next) {
        // Skip entries excluded by the bus and successes from earlier attempts.
        if (!entry->BindingPending) {
            continue;
        }
        
        status = DmDeviceEnableDriverBinding(*entry->DeviceId);
        if (status != OS_EOK) {
            WARNING("DmPublicationEnableBinding: device %u binding failed: %u; retaining group for retry",
                    *entry->DeviceId, status);
            // Earlier drivers may be running; leave their entries intact for retry.
            return status;
        }
        entry->BindingPending = 0;
        TRACE("DmPublicationEnableBinding: device %u binding enabled", *entry->DeviceId);
    }
    return OS_EOK;
}

oserr_t
DmPublicationRemove(
    _InOut_ struct DmPublicationGroup* group)
{
    struct __DmPublicationEntry* entry;
    oserr_t                     status;
    TRACE("DmPublicationRemove(group=%p, state=%i)", group, group->State);

    group->State = DmPublicationRemoving;
    while (group->Last != NULL) {
        entry = group->Last;
        
        status = DmDeviceDestroy(*entry->DeviceId);
        if (status != OS_EOK) {
            WARNING("DmPublicationRemove: device %u removal failed: %u; retaining remaining entries",
                    *entry->DeviceId, status);
            // A busy child still needs its parent and provider. Keep this entry and
            // all earlier entries until their users have finished.
            return status;
        }
        
        TRACE("DmPublicationRemove: removed device %u", *entry->DeviceId);
        *entry->DeviceId = UUID_INVALID;
        
        group->Last = entry->Previous;
        if (group->Last != NULL) {
            // The tail must not point at the entry being freed.
            group->Last->Next = NULL;
        } else {
            group->First = NULL;
        }
        free(entry);
    }
    group->State = DmPublicationRemoved;
    return OS_EOK;
}

oserr_t
DmPublicationReset(
    _InOut_ struct DmPublicationGroup* group)
{
    TRACE("DmPublicationReset(group=%p, state=%i)", group, group->State);
    // Reuse is safe only after every old ID and provider reference is released.
    if (group->State != DmPublicationRemoved) {
        WARNING("DmPublicationReset: group %p cannot reset in state %i", group, group->State);
        return OS_EBUSY;
    }
    group->State = DmPublicationCollecting;
    return OS_EOK;
}
