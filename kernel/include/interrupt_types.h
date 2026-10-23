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
 */

#ifndef __VALI_INTERRUPT_TYPES_H__
#define __VALI_INTERRUPT_TYPES_H__

#include <ddk/interrupt.h>

// Group handles end in these ten bits. Dispatch slots with this suffix must
// remain unavailable, including when an architecture adds software-only slots.
#define INTERRUPT_GROUP_TAG 1023U

#define INTERRUPT_KERNEL 0x10000000U

typedef struct InterruptMessage {
    uint64_t Address;
    uint32_t Data;
} InterruptMessage_t;

typedef struct InterruptDescriptor {
    // Kernel handlers receive Context directly. For user handlers,
    // InterruptBind maps the resources in ResourceTable into their process.
    InterruptResourceTable_t ResourceTable;
    void*                    Context;

    // The platform may replace Line with the line number used by the hardware.
    unsigned int AcpiConform;
    int          Line;
    int          Pin;

    // Candidate lines for kernel devices such as HPET. INTERRUPT_NONE ends the list.
    int Vectors[INTERRUPT_MAXVECTORS];

    // Standalone MSI callers receive the same encoding as message groups.
    uint64_t  MsiAddress;
    uintptr_t MsiValue;
} InterruptDescriptor_t;

// Represents the resources and routing details for one interrupt controller.
typedef struct InterruptControllerResource {
    struct SystemInterruptController* Controller;
    uintptr_t                         Token;
    uuid_t                            TableIndex;
    unsigned int                      Flags;
    unsigned int                      AcpiConform;
    int                               Line;
    int                               Pin;
    int                               Source;
    InterruptMessage_t                Message;
} InterruptControllerResource_t;

#endif //!__VALI_INTERRUPT_TYPES_H__
