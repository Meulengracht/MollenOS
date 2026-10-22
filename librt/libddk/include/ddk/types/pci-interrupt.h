/**
 * Copyright 2026, Philip Meulengracht
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
 * PCI interrupt reservation requests shared by libddk and deviced.
 */

#ifndef __DDK_PCI_INTERRUPT_H__
#define __DDK_PCI_INTERRUPT_H__

#include <os/osdefs.h>

enum PciInterruptOperation {
    PciInterruptReserve,
    PciInterruptProgram,
    PciInterruptEnable,
    PciInterruptDisable,
    PciInterruptRelease
};

struct PciInterruptSupport {
    // Supported interrupt strategies (bitmask of INTERRUPT_STRATEGY_*)
    uint32_t Strategies;
    // Advertised MSI message count
    uint32_t MsiCapacity;
    // Advertised MSI-X table entry count
    uint32_t MsixCapacity;
    // Identifies the host interrupt controller
    uuid_t   ControllerID;
};

/**
 * @brief Reserve and program PCI MSI or MSI-X through OSIOCTLREQUEST_PCI_INTERRUPT.
 *
 * Strategy selects exactly INTERRUPT_STRATEGY_MSI or INTERRUPT_STRATEGY_MSIX.
 * Allocation is the owning kernel group handle; it does not grant device access.
 * Reserve selects Count messages (one for MSI, up to the table capacity for MSI-X).
 * Program supplies the kernel message at Index, which maps directly to the MSI-X
 * table index and must be zero for MSI. Count is used only by Reserve;
 * Index/address/data are used only by Program. Unused fields and Reserved must
 * be zero.
 * Fixed-width fields keep the layout identical on i386, amd64 and ARM64.
 * Rebuild libddk and deviced together when this wire contract changes.
 *
 * Reserve suppresses INTx and message delivery. MSI-X is enabled under Function
 * Mask and every table entry is masked. Enable requires all selected messages
 * programmed and kernel handlers ready. Disable stops message delivery without
 * restoring INTx. Stop device work before Disable/Release; register readback
 * orders writes but cannot drain messages already in flight.
 *
 * All operations can be retried with the same Allocation and Strategy. Program
 * retries must carry the same payload and are rejected while delivery may be
 * active. A failed operation can retain ownership. Once Release starts, only
 * Disable and Release are accepted. Release restores saved message registers or
 * the table, leaves the message mode disabled, and honors the latest accepted
 * bus-control request. Keep the kernel allocation until Release succeeds.
 */
struct PciInterruptRequest {
    uint32_t Operation;
    uuid_t   Allocation;
    uint32_t Strategy;
    uint32_t Count;
    uint32_t Index;
    uint32_t AddressLow;
    uint32_t AddressHigh;
    uint32_t Data;
    uint32_t Reserved;
};

#endif
