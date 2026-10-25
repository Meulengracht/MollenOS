/**
 * Copyright 2026, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#ifndef __DDK_TYPES_INTERRUPT_SOURCE_H__
#define __DDK_TYPES_INTERRUPT_SOURCE_H__

#include <ddk/types/interrupt.h>

/**
 * OSIOCTLREQUEST_INTERRUPT_QUERY selects one source from a platform device's
 * firmware interrupt list. Set Index and zero all other input fields. Success
 * returns the same Index with either FixedLine and its ControllerID/Line, or
 * Message and its upstream message-controller ID with an entirely zero Line.
 * The query does not reserve or activate the source and leaves input unchanged
 * on failure. Prepare the device first when its provider requires activation.
 */
struct DeviceInterruptSourceQuery {
    // Firmware-list index, independent of the provider's hardware source number.
    uint32_t              Index;
    InterruptSourceKind_t Kind;
    uuid_t                ControllerID;
    DeviceInterruptLine_t Line;
};

enum DeviceInterruptSourceOperation {
    // Attach one driver-owned group without enabling its delivery.
    DeviceInterruptSourceReserve,
    // Permit provider delivery after all kernel handlers have been enabled.
    DeviceInterruptSourceEnable,
    // Stop upstream delivery before kernel cleanup; direct GIC stop is in kernel.
    DeviceInterruptSourceDisable,
    // Close the attachment after handler cleanup; keep the kernel group for now.
    DeviceInterruptSourceRelease
};

/**
 * OSIOCTLREQUEST_INTERRUPT_CONTROL lifecycle for one selected platform source.
 * Reserve uses Index and a one-vector kernel allocation. Later operations use
 * only Allocation and require Index == 0, even for a nonzero selected source.
 * The provider obtains the verified process identity from transport metadata;
 * an allocation handle alone grants no access to another driver's resources.
 *
 * Providers own source-to-message translation and obtain message contents from
 * kernel attachment, so child drivers never submit upstream addresses or data.
 * An uncertain reply can conceal ownership: retain Allocation and retry Disable
 * and Release before releasing the kernel group. Exact retries cannot redirect
 * an allocation to another source. Release is terminal for that attachment.
 */
struct DeviceInterruptSourceRequest {
    // One DeviceInterruptSourceOperation value.
    uint32_t Operation;
    // Driver-owned kernel group; never UUID_INVALID.
    uuid_t   Allocation;
    uint32_t Index;
};

#endif //!__DDK_TYPES_INTERRUPT_SOURCE_H__
