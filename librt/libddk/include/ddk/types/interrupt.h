/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *
 * Interrupt Support Definitions & Structures
 * - This header describes the base interrupt-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_TYPES_INTERRUPT_H__
#define __DDK_TYPES_INTERRUPT_H__

#include <ddk/interrupt.h>

typedef struct InterruptGroupRequest {
    uuid_t                ControllerID;
    DeviceInterruptLine_t Line;
    unsigned int          Flags;
    uint32_t              Minimum;
    uint32_t              Optimal;
    uint32_t              Strategies;
} InterruptGroupRequest_t;

typedef struct InterruptGroupResult {
    uuid_t   Handle;
    uint32_t Count;
    uint32_t Strategy;
    
    // Single-message MSI payload. Zero for INTx.
    // Use the indexed message query for individual vectors.
    uint32_t AddressLow;
    uint32_t AddressHigh;
    uint32_t Data;
} InterruptGroupResult_t;

typedef struct InterruptBindRequest {
    uint32_t                 Index;
    InterruptResourceTable_t Resources;
} InterruptBindRequest_t;

typedef enum InterruptMessageControllerKind {
    // The platform's shared PCI message fabric, currently provided on x86.
    InterruptMessageControllerKindPlatform = 1,
    // A PCI host node in the boot Device Tree; never a deviced-generated ID.
    InterruptMessageControllerKindDeviceTree
} InterruptMessageControllerKind_t;

typedef struct InterruptMessageControllerIdentity {
    // Message controller version, should be 1
    uint32_t                         Version;
    InterruptMessageControllerKind_t Kind;
    // FirmwareNode is the host node's byte offset in the 
    // boot Device Tree structure, or zero for Platform
    uint32_t                         FirmwareNode;
    // Must be 0
    uint32_t                         Reserved;
} InterruptMessageControllerIdentity_t;

#endif //!__DDK_TYPES_INTERRUPT_H__
