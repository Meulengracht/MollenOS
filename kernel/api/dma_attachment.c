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

//#define __TRACE

#include <capabilities.h>
#include <debug.h>
#include <dma/attachment.h>
#include <memoryspace.h>

oserr_t
ScDmaManagerCreate(
    _Out_ uuid_t* managerOut)
{
    DMAManager_t* manager = NULL;
    uuid_t        handle;
    oserr_t       status;
    TRACE("ScDmaManagerCreate(managerOut=%p)", managerOut);

    status = CapabilityVerify(CAPABILITY_DEVICEIO_MANAGEMENT);
    if (status != OS_EOK) {
        ERROR("ScDmaManagerCreate: CapabilityVerify failed: %u", status);
        return status;
    }

    status = DMAManagerPrepareCreate(&manager, &handle);
    if (status != OS_EOK) {
        ERROR("ScDmaManagerCreate: DMAManagerPrepareCreate failed: %u", status);
        return status;
    }

    // Only enable the manager after copying its handle to user space succeeds.
    // If the caller closes the handle right before publish, cancel cleanly (the
    // DDK wrapper leaves the caller's output untouched on error).
    status = MemorySpaceCopyToUser(managerOut, &handle, sizeof(handle));
    if (status != OS_EOK) {
        ERROR("ScDmaManagerCreate: MemorySpaceCopyToUser manager %u failed: %u", handle, status);
        goto cancel;
    }

    status = DMAManagerPublish(manager);
    if (status != OS_EOK) {
        ERROR("ScDmaManagerCreate: DMAManagerPublish manager %u failed: %u", handle, status);
        goto cancel;
    }
    goto exit;

cancel:
    DMAManagerCancelCreate(manager);

exit:
    DMAManagerRelease(&manager);
    return status;
}

oserr_t
ScDmaAttachmentRegister(
    _In_  uuid_t                             managerID,
    _In_  const OSDmaAttachmentParameters_t* userParameters,
    _Out_ OSDmaAttachmentId_t*               attachmentOut)
{
    DMAManager_t*                manager = NULL;
    DMAAttachmentRegistration_t* registration = NULL;
    OSDmaAttachmentParameters_t  parameters;
    OSDmaAttachmentId_t          attachment;
    oserr_t                      status;
    TRACE("ScDmaAttachmentRegister(manager=%u)", managerID);

    status = DMAManagerReference(managerID, &manager);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentRegister: DMAManagerReference manager %u failed: %u", managerID, status);
        return status;
    }

    // One fixed input copy gives validation and publication the same description.
    status = MemorySpaceCopyFromUser(&parameters, userParameters, sizeof(parameters));
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentRegister: MemorySpaceCopyFromUser parameters for manager %u failed: %u",
              managerID, status);
        goto exit;
    }

    status = DMAManagerPrepareAttachment(manager, &parameters, &registration, &attachment);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentRegister: DMAManagerPrepareAttachment manager %u, device %u failed: %u",
              managerID, parameters.Device, status);
        goto exit;
    }

    // UpdateLock keeps other registrations from running while we copy the ID
    // to user space. The manager's main Lock stays unlocked so a concurrent close
    // or new event can still happen and block publication.
    status = MemorySpaceCopyToUser(attachmentOut, &attachment, sizeof(attachment));
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentRegister: MemorySpaceCopyToUser attachment %llu for manager %u failed: %u",
              (unsigned long long)attachment.Value, managerID, status);
        goto cancel;
    }

    status = DMAManagerPublishAttachment(&registration);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentRegister: DMAManagerPublishAttachment attachment %llu for manager %u failed: %u",
              (unsigned long long)attachment.Value, managerID, status);
        goto cancel;
    }
    goto exit;

cancel:
    DMAManagerCancelAttachment(&registration);

exit:
    DMAManagerRelease(&manager);
    return status;
}

oserr_t
ScDmaManagerReadEvents(
    _In_  uuid_t                  managerID,
    _Out_ OSDmaAttachmentEvent_t* entries,
    _In_  uint32_t                capacity,
    _Out_ uint32_t*               countOut)
{
    DMAManager_t*             manager = NULL;
    DMAAttachmentEventBatch_t batch;
    oserr_t                   status;
    oserr_t                   finishStatus;
    TRACE("ScDmaManagerReadEvents(manager=%u, capacity=%u)", managerID, capacity);

    status = DMAManagerReference(managerID, &manager);
    if (status != OS_EOK) {
        ERROR("ScDmaManagerReadEvents: DMAManagerReference manager %u failed: %u", managerID, status);
        return status;
    }

    status = DMAManagerSnapshotEvents(manager, capacity, &batch);
    if (status != OS_EOK) {
        ERROR("ScDmaManagerReadEvents: DMAManagerSnapshotEvents manager %u, capacity %u failed: %u",
              managerID, capacity, status);
        goto exit;
    }

    // Copy events to user space without holding any manager lock. If copying
    // the array or the count fails, leave every event queued with its sequence
    // number intact.
    if (batch.Count != 0) {
        status = MemorySpaceCopyToUser(entries, batch.Entries, batch.Count * sizeof(*entries));
        if (status != OS_EOK) {
            ERROR("ScDmaManagerReadEvents: MemorySpaceCopyToUser %u events for manager %u failed: %u",
                  batch.Count, managerID, status);
            goto finish;
        }
    }

    status = MemorySpaceCopyToUser(countOut, &batch.Count, sizeof(batch.Count));
    if (status != OS_EOK) {
        ERROR("ScDmaManagerReadEvents: MemorySpaceCopyToUser count for manager %u failed: %u",
              managerID, status);
        goto finish;
    }

finish:
    // Finish every snapshot, but preserve an earlier copy error.
    finishStatus = DMAManagerFinishEvents(manager, &batch, status == OS_EOK);
    if (finishStatus != OS_EOK) {
        ERROR("ScDmaManagerReadEvents: DMAManagerFinishEvents manager %u failed: %u (copy status %u)",
              managerID, finishStatus, status);
        if (status == OS_EOK) {
            status = finishStatus;
        }
    }

exit:
    DMAManagerRelease(&manager);
    return status;
}

oserr_t
ScDmaAttachmentRemove(
    _In_ uuid_t                     managerID,
    _In_ const OSDmaAttachmentId_t* userAttachment)
{
    DMAManager_t*       manager = NULL;
    OSDmaAttachmentId_t attachment;
    oserr_t             status;
    TRACE("ScDmaAttachmentRemove(manager=%u)", managerID);

    status = DMAManagerReference(managerID, &manager);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentRemove: DMAManagerReference manager %u failed: %u", managerID, status);
        return status;
    }

    // Keep all 64-bit inputs in copied structures on every syscall ABI.
    status = MemorySpaceCopyFromUser(&attachment, userAttachment, sizeof(attachment));
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentRemove: MemorySpaceCopyFromUser attachment for manager %u failed: %u",
              managerID, status);
        goto exit;
    }

    status = DMAManagerRemoveAttachment(manager, attachment.Value);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentRemove: DMAManagerRemoveAttachment attachment %llu for manager %u failed: %u",
              (unsigned long long)attachment.Value, managerID, status);
        goto exit;
    }

exit:
    DMAManagerRelease(&manager);
    return status;
}

oserr_t
ScDmaAttachmentBind(
    _In_  uuid_t                       managerID,
    _In_  const OSDmaBindParameters_t* userParameters,
    _Out_ OSDmaBindingResult_t*        bindingOut)
{
    DMAManager_t*             manager = NULL;
    DMABindingRegistration_t* registration = NULL;
    OSDmaBindParameters_t     parameters;
    OSDmaBindingResult_t      binding;
    oserr_t                   status;
    TRACE("ScDmaAttachmentBind(manager=%u)", managerID);

    status = DMAManagerReference(managerID, &manager);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentBind: DMAManagerReference manager %u failed: %u", managerID, status);
        return status;
    }

    status = MemorySpaceCopyFromUser(&parameters, userParameters, sizeof(parameters));
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentBind: MemorySpaceCopyFromUser parameters for manager %u failed: %u",
              managerID, status);
        goto exit;
    }

    status = DMAManagerPrepareBinding(manager, &parameters, &registration, &binding);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentBind: DMAManagerPrepareBinding attachment %llu for manager %u failed: %u",
              (unsigned long long)parameters.Attachment, managerID, status);
        goto exit;
    }

    // The service only gets back the handle ID and generation, while the handle
    // itself lives in the driver's handle table. On error, cancel inside that
    // driver security group because the calling service never owned the handle.
    status = MemorySpaceCopyToUser(bindingOut, &binding, sizeof(binding));
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentBind: MemorySpaceCopyToUser binding %u for manager %u failed: %u",
              binding.Handle, managerID, status);
        goto cancel;
    }

    status = DMAManagerPublishBinding(&registration);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentBind: DMAManagerPublishBinding binding %u for manager %u failed: %u",
              binding.Handle, managerID, status);
        goto cancel;
    }
    goto exit;

cancel:
    DMAManagerCancelBinding(&registration);

exit:
    DMAManagerRelease(&manager);
    return status;
}

oserr_t
ScDmaAttachmentPrepareStop(
    _In_  uuid_t                     managerID,
    _In_  const OSDmaAttachmentId_t* userAttachment,
    _Out_ OSDmaStopTicket_t*         ticketOut)
{
    DMAManager_t*       manager;
    OSDmaAttachmentId_t attachment;
    OSDmaStopTicket_t   ticket;
    oserr_t             status;

    status = DMAManagerReference(managerID, &manager);
    if (status != OS_EOK) {
        ERROR("%s: DMAManagerReference manager %u failed: %u", __func__, managerID, status);
        return status;
    }

    status = MemorySpaceCopyFromUser(&attachment, userAttachment, sizeof(attachment));
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentPrepareStop: MemorySpaceCopyFromUser manager %u failed: %u", managerID, status);
        goto exit;
    }

    status = DMAManagerPrepareStop(manager, attachment.Value, &ticket);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentPrepareStop: DMAManagerPrepareStop manager %u, attachment %llu failed: %u",
              managerID, (unsigned long long)attachment.Value, status);
        goto exit;
    }

    // If copying the ticket fails, the recovery group stays blocked. Retrying
    // PrepareStop returns the same ticket without reopening DMA.
    status = MemorySpaceCopyToUser(ticketOut, &ticket, sizeof(ticket));
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentPrepareStop: MemorySpaceCopyToUser manager %u, group %llu, epoch %llu failed: %u",
              managerID, (unsigned long long)ticket.RecoveryGroup, (unsigned long long)ticket.Epoch, status);
    }

exit:
    DMAManagerRelease(&manager);
    return status;
}

oserr_t
ScDmaAttachmentCompleteStop(
    _In_ uuid_t                   managerID,
    _In_ const OSDmaStopTicket_t* userTicket)
{
    DMAManager_t*     manager;
    OSDmaStopTicket_t ticket;
    oserr_t           status;

    status = DMAManagerReference(managerID, &manager);
    if (status != OS_EOK) {
        ERROR("%s: DMAManagerReference manager %u failed: %u", __func__, managerID, status);
        return status;
    }

    status = MemorySpaceCopyFromUser(&ticket, userTicket, sizeof(ticket));
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentCompleteStop: MemorySpaceCopyFromUser manager %u failed: %u", managerID, status);
        goto exit;
    }

    status = DMAManagerCompleteStop(manager, &ticket);
    if (status != OS_EOK) {
        ERROR("ScDmaAttachmentCompleteStop: DMAManagerCompleteStop manager %u, group %llu, epoch %llu failed: %u",
              managerID, (unsigned long long)ticket.RecoveryGroup, (unsigned long long)ticket.Epoch, status);
    }

exit:
    DMAManagerRelease(&manager);
    return status;
}
