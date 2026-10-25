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

#ifndef __DDK_TYPES_PCI_INTERRUPT_H__
#define __DDK_TYPES_PCI_INTERRUPT_H__

#include <os/osdefs.h>

// PCI policy values shared by driver allocation and provider wire requests.
// AllocatePci accepts a nonempty subset; GetPciMode and provider requests use
// one selected bit. These values are independent of InterruptSourceKind.
#define INTERRUPT_STRATEGY_INTx 0x1
#define INTERRUPT_STRATEGY_MSI  0x2
#define INTERRUPT_STRATEGY_MSIX 0x4
#define INTERRUPT_STRATEGY_ANY  0x7

enum PciInterruptOperation {
    // Claim the device's selected message mode and leave delivery masked.
    PciInterruptReserve,
    // Program one vector using the message read from its kernel allocation.
    PciInterruptProgram,
    // Permit delivery after all messages and kernel handlers are ready.
    PciInterruptEnable,
    // Mask delivery while retaining the device reservation and saved state.
    PciInterruptDisable,
    // Restore saved state with message delivery off and relinquish ownership.
    PciInterruptRelease
};

/**
 * OSIOCTLREQUEST_PCI_INTERRUPT_QUERY result describing PCI capability policy.
 * Initialize the query buffer to zero. This reports capabilities and the host's
 * message allocator; it does not reserve vectors, negotiate available capacity,
 * or describe platform firmware sources. Strategies uses INTERRUPT_STRATEGY_*
 * values from this wire header, independently of InterruptSourceKind.
 */
struct PciInterruptSupport {
    // Supported interrupt strategies (bitmask of INTERRUPT_STRATEGY_*)
    uint32_t Strategies;
    // Advertised hardware MSI capacity; the current driver API uses one message.
    uint32_t MsiCapacity;
    // Advertised MSI-X table entry count
    uint32_t MsixCapacity;
    // Resolved kernel message-controller ID used by MSI and MSI-X allocations.
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
    // One PciInterruptOperation value.
    uint32_t Operation;
    // Driver-owned kernel message group, retained until provider release succeeds.
    uuid_t   Allocation;
    // One MSI or MSI-X strategy, not a preference mask and never INTx.
    uint32_t Strategy;
    // Reserve only: required vector count. Zero for every later operation.
    uint32_t Count;
    // Program only: allocation vector/MSI-X table index; zero for single MSI.
    uint32_t Index;
    // Program only: kernel-composed destination and payload; zero otherwise.
    uint32_t AddressLow;
    uint32_t AddressHigh;
    uint32_t Data;
    uint32_t Reserved; // Must be zero for every operation.
};

#endif //!__DDK_TYPES_PCI_INTERRUPT_H__
