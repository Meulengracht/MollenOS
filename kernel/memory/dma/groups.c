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
 * DMA recovery groups. Devices that share a bus requester ID or a hardware
 * reset line must be stopped and cleaned up together, so they belong to the
 * same recovery group. Adding an attachment can merge existing groups, and
 * removing one can split a group back into smaller pieces. Updates are built
 * outside manager->Lock before swapping pointers under the lock.
 */

#include <assert.h>
#include <heap.h>
#include <limits.h>
#include <string.h>

#include "attachment_private.h"

// Adds an internal reference to keep the group struct allocated. Fails with
// OS_ECANCELLED if the count already dropped to zero.
oserr_t
DMARecoveryGroupAcquire(
    _In_ DMARecoveryGroup_t* group)
{
    unsigned int references = atomic_load(&group->References);

    do {
        if (references == 0) {
            return OS_ECANCELLED;
        }
        if (references == UINT_MAX) {
            return OS_EOVERFLOW;
        }
    } while (!atomic_compare_exchange_weak(&group->References, &references, references + 1));
    return OS_EOK;
}

// Drops one group reference and frees the group once the last reference goes.
// Every running operation and stop pass must be done before the final drop.
void
DMARecoveryGroupRelease(
    _InOut_ DMARecoveryGroup_t** group)
{
    DMARecoveryGroup_t* current = *group;

    if (current == NULL) {
        return;
    }

    // always clear the reference at this point
    *group = NULL;

    if (atomic_fetch_sub(&current->References, 1) == 1) {
        assert(current->Access.Operations == 0);
        assert(current->Stop.Phase == DMARecoveryStopIdle);
        MutexDestruct(&current->Lock);
        kfree(current);
    }
}

// Initialize Access.Lock and its atomic gate fields before publishing the
// group so fast DMA entry paths can check state without taking group->Lock.
static oserr_t
__PrepareGroupObjects(
    _In_ DMARecoveryUpdate_t* update)
{
    for (uint32_t i = 0; i < update->Count; i++) {
        uint64_t            id = update->Entries[i].RecoveryGroup;
        DMARecoveryGroup_t* group;
        bool                found = false;

        if (id == 0) {
            continue;
        }
        for (uint32_t j = 0; j < update->NewCount; j++) {
            if (update->NewGroups[j]->ID == id) {
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }
        group = kmalloc(sizeof(DMARecoveryGroup_t));
        if (group == NULL) {
            return OS_EOOM;
        }
        memset(group, 0, sizeof(DMARecoveryGroup_t));

        ELEMENT_INIT(&group->Link, 0, group);
        group->Manager = update->Manager;
        group->ID = id;
        SpinlockConstruct(&group->Access.Lock);
        atomic_init(&group->Access.State, DMARecoveryLive);
        atomic_init(&group->Access.Operations, 0);
        atomic_init(&group->Access.ManagerClosed, false);
        group->Stop.Round = 1;
        group->Stop.CancellationError = OS_EOK;
        atomic_init(&group->References, 1);
        MutexConstruct(&group->Lock, MUTEX_FLAG_PLAIN);

        update->NewGroups[update->NewCount++] = group;
    }
    return OS_EOK;
}

static bool
__SharesRequesterOrResetGroup(
    _In_ const DMAAttachment_t* first,
    _In_ const DMAAttachment_t* second)
{
    // Match requester keys only with requester keys, and reset keys only with reset keys.
    return first->Configuration.RequesterGroup == second->Configuration.RequesterGroup ||
           first->Configuration.ResetGroup == second->Configuration.ResetGroup;
}

// Hold manager->Lock. Reserve consecutive IDs without wrapping the counter.
// A failed update may leave a gap in the ID numbers, which is harmless.
static oserr_t
__ReserveRecoveryGroupIDs(
    _In_  DMAManager_t* manager,
    _In_  uint32_t      count,
    _Out_ uint64_t*     firstGroupIDOut)
{
    uint64_t firstGroupID = manager->Registry.NextRecoveryGroupID;

    if (count > UINT64_MAX - firstGroupID) {
        return OS_EOVERFLOW;
    }

    manager->Registry.NextRecoveryGroupID += count;
    *firstGroupIDOut = firstGroupID;
    return OS_EOK;
}

static bool
__ContainsRecoveryGroup(
    _In_ const uint64_t* groups,
    _In_ uint32_t        groupCount,
    _In_ uint64_t        recoveryGroupID)
{
    for (uint32_t i = 0; i < groupCount; i++) {
        if (groups[i] == recoveryGroupID) {
            return true;
        }
    }
    return false;
}

oserr_t
DMARecoveryPrepareRegistration(
    _In_  DMAManager_t*         manager,
    _In_  DMAAttachment_t*      addedAttachment,
    _Out_ DMARecoveryUpdate_t** updateOut)
{
    DMARecoveryUpdate_t* update;
    DMAAttachment_t*     attachment;
    uint64_t             connectedGroups[DMA_MANAGER_MAX_ATTACHMENTS];
    uint64_t             mergedGroupID;
    uint32_t             connectedGroupCount = 0;
    oserr_t              status;

    update = kmalloc(sizeof(DMARecoveryUpdate_t));
    if (update == NULL) {
        return OS_EOOM;
    }

    memset(update, 0, sizeof(DMARecoveryUpdate_t));
    update->Manager = manager;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    // Existing recovery groups already link attachments connected indirectly
    // through other attachments. Find every group that shares a key with the
    // new attachment, then gather all members of those groups so they merge.
    foreach (node, &manager->Registry.Attachments) {
        attachment = node->value;
        if (__SharesRequesterOrResetGroup(attachment, addedAttachment)) {
            connectedGroups[connectedGroupCount++] = attachment->RecoveryGroup->ID;
        }
    }

    foreach (node, &manager->Registry.Attachments) {
        attachment = node->value;
        if (__ContainsRecoveryGroup(
                connectedGroups,
                connectedGroupCount,
                attachment->RecoveryGroup->ID)) {
            update->Entries[update->Count++].Attachment = attachment;
        }
    }

    assert(update->Count < DMA_MANAGER_MAX_ATTACHMENTS);
    update->Entries[update->Count++].Attachment = addedAttachment;

    status = __ReserveRecoveryGroupIDs(manager, 1, &mergedGroupID);
    if (status != OS_EOK) {
        goto unlock;
    }

    for (uint32_t i = 0; i < update->Count; i++) {
        update->Entries[i].RecoveryGroup = mergedGroupID;
    }

unlock:
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        DMARecoveryDiscardUpdate(&update);
        return status;
    }

    // Allocate the new group structs outside manager->Lock so running out of
    // memory leaves existing groups untouched.
    status = __PrepareGroupObjects(update);
    if (status != OS_EOK) {
        DMARecoveryDiscardUpdate(&update);
        return status;
    }

    *updateOut = update;
    return OS_EOK;
}

// Expects entry 0 to be the removed attachment and all group fields to start at 0.
// Walks connected attachments to assign temporary group labels (1 through
// groupCount), comparing each queued entry against the remaining candidates.
static uint32_t
__LabelRemainingRecoveryGroups(
    _In_ DMARecoveryUpdate_t* update)
{
    uint32_t pendingEntries[DMA_MANAGER_MAX_ATTACHMENTS];
    uint32_t groupCount = 0;

    // Leave entry 0 at zero to mark the removed attachment.
    for (uint32_t startIndex = 1; startIndex < update->Count; startIndex++) {
        uint32_t readIndex = 0;
        uint32_t writeIndex = 0;
        
        if (update->Entries[startIndex].RecoveryGroup != 0) {
            continue;
        }

        update->Entries[startIndex].RecoveryGroup = ++groupCount;

        pendingEntries[writeIndex++] = startIndex;
        while (readIndex != writeIndex) {
            uint32_t currentIndex = pendingEntries[readIndex++];
            
            for (uint32_t candidateIndex = 1; candidateIndex < update->Count; candidateIndex++) {
                if (update->Entries[candidateIndex].RecoveryGroup != 0) {
                    continue;
                }
                
                if (__SharesRequesterOrResetGroup(
                        update->Entries[currentIndex].Attachment,
                        update->Entries[candidateIndex].Attachment)) {
                    update->Entries[candidateIndex].RecoveryGroup = groupCount;
                    pendingEntries[writeIndex++] = candidateIndex;
                }
            }
        }
    }
    return groupCount;
}

oserr_t
DMARecoveryPrepareRemoval(
    _In_  DMAManager_t*         manager,
    _In_  DMAAttachment_t*      removedAttachment,
    _Out_ DMARecoveryUpdate_t** updateOut)
{
    DMARecoveryUpdate_t* update;
    DMAAttachment_t*     attachment;
    uint64_t             firstGroupID;
    uint32_t             groupCount;
    oserr_t              status;

    update = kmalloc(sizeof(DMARecoveryUpdate_t));
    if (update == NULL) {
        return OS_EOOM;
    }

    memset(update, 0, sizeof(DMARecoveryUpdate_t));
    update->Manager = manager;
    update->Entries[0].Attachment = removedAttachment;
    update->Count = 1;

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    foreach (node, &manager->Registry.Attachments) {
        attachment = node->value;
        if (attachment != removedAttachment &&
            attachment->RecoveryGroup == removedAttachment->RecoveryGroup) {
            update->Entries[update->Count++].Attachment = attachment;
        }
    }
    MutexUnlock(&manager->Lock);

    // UpdateLock keeps attachment pointers valid while we calculate without Lock.
    // Events can still arrive and the manager can close during this step.
    groupCount = __LabelRemainingRecoveryGroups(update);

    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    status = __ReserveRecoveryGroupIDs(manager, groupCount, &firstGroupID);
    if (status != OS_EOK) {
        goto unlock;
    }

    // Convert the temporary labels to manager-local IDs. Entry zero stays zero.
    for (uint32_t i = 1; i < update->Count; i++) {
        update->Entries[i].RecoveryGroup += firstGroupID - 1;
    }

unlock:
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        DMARecoveryDiscardUpdate(&update);
        return status;
    }

    status = __PrepareGroupObjects(update);
    if (status != OS_EOK) {
        DMARecoveryDiscardUpdate(&update);
        return status;
    }

    *updateOut = update;
    return OS_EOK;
}

// Moves the prepared recovery groups into the manager's list and updates each
// affected attachment's RecoveryGroup pointer. Old groups are moved into
// update->OldGroups so DMARecoveryDiscardUpdate can drop their registry
// references after manager->Lock is released.
void
DMARecoveryApplyUpdate(
    _In_ DMARecoveryUpdate_t* update)
{
    // Move the old registry references into the update so DiscardUpdate frees
    // them after manager->Lock is released (including multi-member groups).
    for (uint32_t i = 0; i < update->Count; i++) {
        DMARecoveryGroup_t* old = update->Entries[i].Attachment->RecoveryGroup;
        DMARecoveryGroup_t* replacement = NULL;
        bool                found = false;

        for (uint32_t j = 0; j < update->OldCount; j++) {
            if (update->OldGroups[j] == old) {
                found = true;
                break;
            }
        }

        if (old != NULL && !found) {
            list_remove(&update->Manager->Registry.Groups, &old->Link);
            update->OldGroups[update->OldCount++] = old;
        }

        for (uint32_t j = 0; j < update->NewCount; j++) {
            if (update->NewGroups[j]->ID == update->Entries[i].RecoveryGroup) {
                replacement = update->NewGroups[j];
                break;
            }
        }

        update->Entries[i].Attachment->RecoveryGroup = replacement;
    }

    for (uint32_t i = 0; i < update->NewCount; i++) {
        list_append(&update->Manager->Registry.Groups, &update->NewGroups[i]->Link);
        update->NewGroups[i] = NULL;
    }
}

void
DMARecoveryDiscardUpdate(
    _InOut_ DMARecoveryUpdate_t** update)
{
    DMARecoveryUpdate_t* current = *update;

    if (current == NULL) {
        return;
    }
    
    *update = NULL;
    
    for (uint32_t i = 0; i < current->NewCount; i++) {
        DMARecoveryGroupRelease(&current->NewGroups[i]);
    }
    
    for (uint32_t i = 0; i < current->OldCount; i++) {
        DMARecoveryGroupRelease(&current->OldGroups[i]);
    }
    kfree(current);
}
