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

#ifndef __DDK_TYPES_INTERRUPT_GROUP_H__
#define __DDK_TYPES_INTERRUPT_GROUP_H__

#include <ddk/types/interrupt.h>

/**
 * Kernel reservation request for one resource kind: one legacy/fixed line or
 * a negotiated set of independently allocated messages. The caller chooses
 * policy before requesting a group; the kernel does not fall back to a different
 * kind. Allocation leaves dispatch disabled and installs no handler mappings.
 *
 * Line kinds require Minimum == Optimal == 1. Message requests require
 * 1 <= Minimum <= Optimal; allocation may return fewer than Optimal, subject
 * to controller capacity and the kernel group limit. Messages are exclusive.
 * Fixed lines require INTERRUPT_ACPICONFORM_FIXED in Line.AcpiConform and a
 * matching controller that preserves the exact source (currently ARM SPIs).
 */
typedef struct InterruptGroupRequest {
    // Zero for legacy lines; resolved controller ID for fixed lines/messages.
    // Only kernel message callers may use zero for the platform default.
    uuid_t                ControllerID;
    // Exact/routed line for line kinds; all fields zero for messages.
    DeviceInterruptLine_t Line;
    // Optional INTERRUPT_EXCLUSIVE; no other driver flags are accepted.
    unsigned int          Flags;
    uint32_t              Minimum;
    uint32_t              Optimal;
    // Exactly one InterruptSourceKind value, never a PCI preference mask.
    InterruptSourceKind_t Kind;
} InterruptGroupRequest_t;

/**
 * Successful reservation owned by the calling process. Handle names the group,
 * not an individual vector or device authorization. Count vectors use indices
 * 0 through Count - 1, and Kind matches the requested resource kind. Read
 * message contents by indexed query. Keep the handle until release succeeds;
 * failed cleanup leaves it available for retry. Stop and close any provider
 * attachment before releasing the group reservation.
 */
typedef struct InterruptGroupResult {
    uuid_t                Handle;
    uint32_t              Count;
    InterruptSourceKind_t Kind;
} InterruptGroupResult_t;

/**
 * Bind one allocation vector while the group is disabled. Index is an allocation
 * index, not a firmware source number. Resources is copied and mapped by the
 * kernel; partial failure can retain mappings requiring Disable or Release.
 * Every vector must be bound before group Enable can succeed.
 */
typedef struct InterruptBindRequest {
    uint32_t                 Index;
    InterruptResourceTable_t Resources;
} InterruptBindRequest_t;

#endif //!__DDK_TYPES_INTERRUPT_GROUP_H__
