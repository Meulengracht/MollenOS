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


#ifndef __DDK_DMA_ATTACHMENT_H__
#define __DDK_DMA_ATTACHMENT_H__

#include <ddk/ddkdefs.h>
#include <os/types/dma_attachment.h>

_CODE_BEGIN

/**
 * @brief Creates the DMA manager handle for the calling service's security group.
 *
 * Only one DMA manager can exist at a time, and the caller needs
 * CAPABILITY_DEVICEIO_MANAGEMENT. Before using it, subscribe to IOSETIN
 * (readable/ready events in your I/O wait set) on the handle and drain any
 * queued events. At shutdown, remove every registered attachment before closing
 * the handle with OSHandleDestroy, since remaining attachments keep the manager
 * alive.
 *
 * @param managerOut Receives the new manager handle on success. Unchanged on error.
 * @return OS_EOK, OS_EBUSY if a manager already exists, or an error if the
 *         caller lacks permission, memory runs out, or copying the handle fails.
 */
DDKDECL(oserr_t,
DMAManagerCreate(
    _Out_ uuid_t* managerOut));

/**
 * @brief Registers a device's DMA settings with the manager.
 *
 * Keep the device's bus routing and address setup valid until the attachment is
 * removed, because the description cannot be edited once registered. Calling
 * this again with the exact same configuration for the same Device returns the
 * existing ID. Passing different settings for an already registered Device
 * fails with OS_EBUSY.
 *
 * @param manager       Live manager handle owned by the calling security group.
 * @param parameters    Device DMA settings. Reserved and unused fields must be 0.
 * @param attachmentOut Receives the manager-local attachment ID on success.
 * @return OS_EOK on success, or an error. attachmentOut is unchanged on failure.
 */
DDKDECL(oserr_t,
DMAAttachmentRegister(
    _In_  uuid_t                             manager,
    _In_  const OSDmaAttachmentParameters_t* parameters,
    _Out_ OSDmaAttachmentId_t*               attachmentOut));

/**
 * @brief Reads a batch of waiting attachment events from the manager.
 *
 * Keep calling this in a loop until countOut is 0. Because the same event can
 * be delivered more than once after a state change or copy failure, check the
 * device's current state before stopping DMA or removing an attachment.
 *
 * @param manager  Live manager handle owned by the calling security group.
 * @param entries  Buffer to fill with event records.
 * @param capacity Number of entries the buffer can hold. Must be at least 1,
 *                 and at most DMA_MANAGER_MAX_EVENTS are returned per call.
 * @param countOut Receives how many events were written on success.
 * @return OS_EOK (including when 0 events are waiting), or an error. Failed
 *         reads leave all events queued and leave entries and countOut intact.
 */
DDKDECL(oserr_t,
DMAManagerReadEvents(
    _In_  uuid_t                  manager,
    _Out_ OSDmaAttachmentEvent_t* entries,
    _In_  uint32_t                capacity,
    _Out_ uint32_t*               countOut));

/**
 * @brief Removes an attachment once its driver binding and events are cleared.
 *
 * Keep the device's bus configuration valid until this returns OS_EOK.
 *
 * @param manager    Live manager handle owned by the calling security group.
 * @param attachment ID returned when the attachment was registered.
 * @return OS_EOK, OS_ENOENT if the ID is unknown, OS_EBUSY if a binding or
 *         queued event still exists, or a permission or memory error.
 */
DDKDECL(oserr_t,
DMAAttachmentRemove(
    _In_ uuid_t                     manager,
    _In_ const OSDmaAttachmentId_t* attachment));

/**
 * @brief Gives a driver process permission to use a registered attachment.
 *
 * Installs a binding handle directly into the target driver's handle table. If
 * a reply gets lost, repeating the call with the same RequestId returns the
 * active binding handle and generation so no duplicate binding is created. Keep
 * the attachment's bus configuration valid while it stays bound.
 *
 * @param manager    Live manager handle owned by the calling security group.
 * @param parameters Attachment ID, target driver's security group ID, and RequestId.
 * @param bindingOut Receives the binding handle and generation on success.
 * @return OS_EOK on a new binding or a valid retry. On failure, bindingOut is
 *         unchanged and any existing active binding stays intact.
 */
DDKDECL(oserr_t,
DMAAttachmentBind(
    _In_  uuid_t                       manager,
    _In_  const OSDmaBindParameters_t* parameters,
    _Out_ OSDmaBindingResult_t*        bindingOut));

/**
 * @brief Blocks an attachment's recovery group and returns a stop ticket.
 *
 * A recovery group holds all attachments that share a bus requester ID or a
 * reset line. Once this runs, no new DMA operations can start anywhere in that
 * group. Physically stop every device in the group before passing the ticket to
 * DMAAttachmentCompleteStop. If copying the ticket back fails, the group stays
 * blocked and calling this again returns the same ticket.
 *
 * @param manager    Live manager handle owned by the calling security group.
 * @param attachment Any attachment in the recovery group to stop.
 * @param ticketOut  Receives the stop ticket on success. Unchanged on failure.
 * @return OS_EOK on success, or an error.
 */
DDKDECL(oserr_t,
DMAAttachmentPrepareStop(
    _In_  uuid_t                     manager,
    _In_  const OSDmaAttachmentId_t* attachment,
    _Out_ OSDmaStopTicket_t*         ticketOut));

/**
 * @brief Confirms that hardware in a recovery group has stopped so memory can be freed.
 *
 * Call this only when you are sure no device in the group can still read or
 * write RAM. Turning off interrupts or getting a message from a driver does not
 * prove transfers have finished, because hardware may still have DMA work in
 * progress.
 *
 * If this returns OS_EBUSY (for example, a system call is still finishing up)
 * or another cleanup error, keep the hardware stopped and retry with the same
 * ticket. Calling this again on a ticket that already finished returns OS_EOK.
 *
 * @param manager Live manager handle owned by the calling security group.
 * @param ticket  Stop ticket returned by DMAAttachmentPrepareStop.
 * @return OS_EOK on success, OS_EBUSY if active work is still finishing, or an error.
 */
DDKDECL(oserr_t,
DMAAttachmentCompleteStop(
    _In_ uuid_t                   manager,
    _In_ const OSDmaStopTicket_t* ticket));

_CODE_END

#endif //!__DDK_DMA_ATTACHMENT_H__
