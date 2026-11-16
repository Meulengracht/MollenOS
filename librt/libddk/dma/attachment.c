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


#include <ddk/dma/attachment.h>
#include <internal/_syscalls.h>
#include <string.h>

oserr_t
DMAManagerCreate(
    _Out_ uuid_t* managerOut)
{
    uuid_t  manager;
    oserr_t status;

    if (managerOut == NULL) {
        return OS_EINVALPARAMS;
    }
    
    status = Syscall_DmaManagerCreate(&manager);
    if (status == OS_EOK) {
        *managerOut = manager;
    }
    return status;
}

oserr_t
DMAAttachmentRegister(
    _In_  uuid_t                             manager,
    _In_  const OSDmaAttachmentParameters_t* parameters,
    _Out_ OSDmaAttachmentId_t*               attachmentOut)
{
    OSDmaAttachmentId_t attachment;
    oserr_t             status;

    if (parameters == NULL || attachmentOut == NULL) {
        return OS_EINVALPARAMS;
    }

    status = Syscall_DmaAttachmentRegister(manager, parameters, &attachment);
    if (status == OS_EOK) {
        *attachmentOut = attachment;
    }
    return status;
}

oserr_t
DMAManagerReadEvents(
    _In_  uuid_t                  manager,
    _Out_ OSDmaAttachmentEvent_t* entries,
    _In_  uint32_t                capacity,
    _Out_ uint32_t*               countOut)
{
    OSDmaAttachmentEvent_t batch[DMA_MANAGER_MAX_EVENTS];
    uint32_t               count;
    oserr_t                status;

    if (entries == NULL || countOut == NULL || capacity == 0) {
        return OS_EINVALPARAMS;
    }

    if (capacity > DMA_MANAGER_MAX_EVENTS) {
        capacity = DMA_MANAGER_MAX_EVENTS;
    }

    // The syscall can fill batch and then fail when copying count or hitting a
    // manager close. Stage both outputs locally so callers never see half-written
    // results on error.
    status = Syscall_DmaManagerReadEvents(manager, batch, capacity, &count);
    if (status == OS_EOK) {
        memcpy(entries, batch, count * sizeof(*entries));
        *countOut = count;
    }
    return status;
}

oserr_t
DMAAttachmentRemove(
    _In_ uuid_t                     manager,
    _In_ const OSDmaAttachmentId_t* attachment)
{
    if (attachment == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_DmaAttachmentRemove(manager, attachment);
}

oserr_t
DMAAttachmentBind(
    _In_  uuid_t                       manager,
    _In_  const OSDmaBindParameters_t* parameters,
    _Out_ OSDmaBindingResult_t*        bindingOut)
{
    OSDmaBindingResult_t binding;
    oserr_t              status;

    if (parameters == NULL || bindingOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // Even if copying the result to user space works, the driver could close its
    // binding before the kernel records it. Return the result only after the
    // kernel confirms the binding.
    status = Syscall_DmaAttachmentBind(manager, parameters, &binding);
    if (status == OS_EOK) {
        *bindingOut = binding;
    }
    return status;
}

oserr_t
DMAAttachmentPrepareStop(
    _In_  uuid_t                     manager,
    _In_  const OSDmaAttachmentId_t* attachment,
    _Out_ OSDmaStopTicket_t*         ticketOut)
{
    OSDmaStopTicket_t ticket;
    oserr_t           status;

    if (attachment == NULL || ticketOut == NULL) {
        return OS_EINVALPARAMS;
    }

    status = Syscall_DmaAttachmentPrepareStop(
        manager,
        attachment,
        &ticket
    );
    if (status == OS_EOK) {
        *ticketOut = ticket;
    }
    return status;
}

oserr_t
DMAAttachmentCompleteStop(
    _In_ uuid_t                   manager,
    _In_ const OSDmaStopTicket_t* ticket)
{
    if (ticket == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_DmaAttachmentCompleteStop(manager, ticket);
}
