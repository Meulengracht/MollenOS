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
 * @brief Create the DMA manager for the calling security group.
 * Requires CAPABILITY_DEVICEIO_MANAGEMENT. Close with OSHandleDestroy after
 * removing all attachments. Subscribe to IOSETIN and drain events before use.
 * @param managerOut Receives the handle only on success.
 * @return OS_EOK, OS_EBUSY if a registry entry exists, or an authorization,
 * allocation or output-copy error.
 */
DDKDECL(oserr_t,
DMAManagerCreate(
    _Out_ uuid_t* managerOut));

/**
 * @brief Register a copied, immutable device description.
 * Keep the bus configuration valid until removal succeeds. An identical retry
 * for Device returns the same ID; changing a registered description is rejected.
 * @param manager Live manager handle in the creating group's grant table.
 * @param parameters Complete description with all reserved fields zero.
 * @param attachmentOut Receives the manager-local ID only on success.
 * @return Registration status. Failure leaves attachmentOut unchanged.
 */
DDKDECL(oserr_t,
DMAAttachmentRegister(
    _In_  uuid_t                             manager,
    _In_  const OSDmaAttachmentParameters_t* parameters,
    _Out_ OSDmaAttachmentId_t*               attachmentOut));

/**
 * @brief Read a bounded batch of pending attachment events.
 * Drain until countOut is zero. Repeated events are possible; check their state
 * through administrative operations before removing an attachment or stopping DMA.
 * @param manager Live manager handle in the creating group's grant table.
 * @param entries Array with room for capacity entries.
 * @param capacity Positive capacity; at most DMA_MANAGER_MAX_EVENTS are returned.
 * @param countOut Receives the number of returned entries only on success.
 * @return OS_EOK, including an empty queue, or an error. Failed reads consume
 * no event and the DDK wrapper leaves entries and countOut unchanged.
 */
DDKDECL(oserr_t,
DMAManagerReadEvents(
    _In_  uuid_t                  manager,
    _Out_ OSDmaAttachmentEvent_t* entries,
    _In_  uint32_t                capacity,
    _Out_ uint32_t*               countOut));

/**
 * @brief Remove an unused attachment after its pending event has been consumed.
 * Stop depending on the bus description only after this call succeeds.
 * @param manager Live manager handle in the creating group's grant table.
 * @param attachment ID previously returned by registration.
 * @return OS_EOK, OS_ENOENT for an absent ID, OS_EBUSY for pending state,
 * or an authorization, validation or allocation error.
 */
DDKDECL(oserr_t,
DMAAttachmentRemove(
    _In_ uuid_t                     manager,
    _In_ const OSDmaAttachmentId_t* attachment));

/**
 * @brief Install a binding grant in the assigned driver's security group.
 * Requires the calling group's live manager grant. Keep the same request ID
 * when retrying a lost reply. The attachment's description must remain valid.
 *
 * @param manager Manager handle in the calling service group.
 * @param parameters Attachment, verified driver principal and request ID.
 * @param bindingOut Receives the handle and generation only on success.
 * @return OS_EOK for a new binding or an identical live retry. Errors leave
 * bindingOut unchanged; they never remove a binding returned by a retry.
 */
DDKDECL(oserr_t,
DMAAttachmentBind(
    _In_  uuid_t                       manager,
    _In_  const OSDmaBindParameters_t* parameters,
    _Out_ OSDmaBindingResult_t*        bindingOut));

_CODE_END

#endif //!__DDK_DMA_ATTACHMENT_H__
