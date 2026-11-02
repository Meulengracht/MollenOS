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
 * A bus provider uses these requests to attach acknowledgement callbacks to a
 * device driver's interrupt group. Ordinary drivers use interrupt.h and do not
 * need to know about the callbacks.
 */

#ifndef __DDK_INTERRUPT_BUS_H__
#define __DDK_INTERRUPT_BUS_H__

#include <ddk/types/interrupt.h>
#include <ddk/types/interrupt-controller.h>

#define INTERRUPT_PROVIDER_MAX_VECTORS 2048U

/**
 * @brief An interrupt's optional bus callback and hardware message.
 *
 * Completion names a callback that runs after the device handler, such as one
 * that acknowledges upstream bus hardware. Set it to zero when no callback is
 * needed. A nonzero completion must be bound and owned by the calling provider.
 * The kernel fills Message with the address and data for this interrupt.
 * Its input value is ignored, so a failed request can be retried with the same
 * buffer. Fixed-line attachments require Completion == UUID_INVALID and return
 * a zero Message. Completion results do not replace the device handler's result.
 */
typedef struct InterruptBusVector {
    uuid_t                   Completion;
    DeviceInterruptMessage_t Message;
} InterruptBusVector_t;

/**
 * @brief Attach provider callbacks to every interrupt in a driver's group.
 *
 * Set Principal from the message transport's verified sender identity. Do not
 * trust a process ID from the driver's request. ControllerID and Count must
 * match the existing allocation. Count must be nonzero and Vectors must address
 * that many readable/writable entries, up to INTERRUPT_PROVIDER_MAX_VECTORS.
 * Vectors follow allocation order, not firmware source or controller ordinals.
 * Message attachments require Line == LineFlags == 0. Fixed-line attachments
 * require Count == 1, an exact line/attribute match, and no completion callback.
 * Legacy/manual line groups cannot attach a provider.
 *
 * Attachment takes a reference to every completion or leaves the group alone.
 * If copying messages back to user memory fails, retrying the same request
 * returns them without taking extra references. A closed group cannot be
 * attached again, even with a delayed copy of the original request.
 * The first attachment requires a disabled, unbound group. An exact retry may
 * retrieve the existing messages after handlers have been bound or enabled;
 * it must preserve provider, device, principal and completion identities.
 */
typedef struct InterruptBusAttachRequest {
    // Group handle allocated by the device driver.
    uuid_t                          Allocation;
    // Driver process identity verified by the provider.
    uuid_t                          Principal;
    // Device identity the provider uses to distinguish its children.
    uuid_t                          Device;
    // Resolved kernel controller ID; never reinterpret it as a firmware key.
    uuid_t                          ControllerID;
    uint32_t                        Count;
    InterruptBusVector_t*           Vectors;
    // Zero for message groups. For an explicitly validated fixed-line group this must
    // match its reserved INTID, and ControllerID must be its line controller.
    // Fixed groups have one vector, no completion callback and no message.
    uint32_t                        Line;
    // Entire reserved AcpiConform value, including PRESENT/FIXED/shareability;
    // zero for message attachments, not merely trigger and polarity bits.
    uint32_t                        LineFlags;
} InterruptBusAttachRequest_t;

#endif //!__DDK_INTERRUPT_BUS_H__
