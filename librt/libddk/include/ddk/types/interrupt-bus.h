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
 * A bus uses these requests to attach callbacks to a device driver's interrupt
 * group. The callbacks acknowledge bus hardware after the device handler runs.
 * Device drivers use interrupt.h without handling the bus callbacks.
 */

#ifndef __DDK_TYPES_INTERRUPT_BUS_H__
#define __DDK_TYPES_INTERRUPT_BUS_H__

#include <ddk/types/interrupt.h>
#include <ddk/types/interrupt-controller.h>

#define INTERRUPT_BUS_MAX_VECTORS 2048U

/**
 * @brief An interrupt's optional bus callback and hardware message.
 *
 * Completion is the handle of a callback that runs after the device handler.
 * Set it to zero when no callback is needed. A nonzero handle must refer to a
 * bound callback created by the calling bus process. The callback can acknowledge
 * upstream hardware, but its return value does not change the device handler's result.
 *
 * The kernel fills Message with the interrupt address and data. It ignores the
 * input value, so a failed request can use the same buffer for a retry. Fixed-line
 * attachments require Completion == UUID_INVALID and return a zero Message.
 */
typedef struct InterruptBusVector {
    uuid_t                   Completion;
    DeviceInterruptMessage_t Message;
} InterruptBusVector_t;

/**
 * @brief Request to attach bus callbacks to every interrupt in a driver's group.
 *
 * Set Principal from the message transport's verified sender identity. Do not
 * trust a process ID from the driver's request. ControllerID and Count must match
 * the existing allocation. Count must be between 1 and INTERRUPT_BUS_MAX_VECTORS,
 * and Vectors must point to that many readable and writable entries. Vectors use
 * allocation order, regardless of firmware source or controller indices.
 *
 * Message attachments require Line == LineFlags == 0. Fixed-line attachments
 * require Count == 1, matching line and attributes, and no callback. Legacy and
 * manually configured line groups cannot attach a bus.
 *
 * Attachment takes a reference to every callback or leaves the group unchanged.
 * If copying messages back to user memory fails, retrying the same request
 * returns them without taking extra references. A closed group cannot be
 * attached again, even with a delayed copy of the original request.
 * Before the first attachment, the group must be disabled and unbound. A retry
 * can retrieve messages after handlers are bound or enabled. It must use the
 * same bus process, device, principal and callback handles as the first request.
 */
typedef struct InterruptBusAttachRequest {
    // Group handle allocated by the device driver.
    uuid_t                          Allocation;
    // Driver process identity verified by the bus.
    uuid_t                          Principal;
    // Device identity the bus uses to distinguish its children.
    uuid_t                          Device;
    // Resolved kernel controller ID. Must not be used as a firmware key.
    uuid_t                          ControllerID;
    uint32_t                        Count;
    InterruptBusVector_t*           Vectors;
    // Zero for message groups. For a validated fixed-line group, this must match
    // its reserved INTID and ControllerID must identify its line controller.
    // Fixed groups have one vector, no callback and no message.
    uint32_t                        Line;
    // Zero for message attachments. Fixed-line attachments need the entire
    // reserved AcpiConform value, including PRESENT, FIXED and sharing flags
    // along with the trigger and polarity bits.
    uint32_t                        LineFlags;
} InterruptBusAttachRequest_t;

#endif //!__DDK_TYPES_INTERRUPT_BUS_H__
