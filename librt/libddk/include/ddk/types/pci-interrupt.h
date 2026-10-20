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
    PciInterruptEnable,
    PciInterruptDisable,
    PciInterruptRelease
};

/**
 * @brief Usable message modes and advertised capacities of this PCI function.
 *
 * OSIOCTLREQUEST_PCI_INTERRUPT_QUERY returns INTERRUPT_STRATEGY_MSI or zero in
 * Strategies. Only Strategies grants permission to attempt a mode. Capacity
 * fields describe the hardware, not the number of currently free kernel vectors:
 * MsiCapacity is the advertised message count, and MsixCapacity is the table
 * entry count. Zero means absent or unavailable under provider/platform policy.
 * MSI-X and multi-message MSI remain unsupported even when their capacities are
 * nonzero. MSI-X BAR/table validation is deferred to its future provider.
 * The query leaves configuration unchanged and does not reserve the function.
 * Rebuild the runtime and deviced together when this fixed-width layout changes.
 */
struct PciInterruptSupport {
    uint32_t Strategies;
    uint32_t MsiCapacity;
    uint32_t MsixCapacity;
};

/**
 * @brief Changes one PCI function's single-message MSI reservation.
 *
 * Allocation identifies the kernel allocation that owns this reservation.
 * Vali does not check device access.
 * Reserve uses the kernel's message in AddressLow, AddressHigh and Data.
 * Fixed-width fields keep the request layout the same on i386, amd64 and ARM64.
 * Reserved must be zero.
 *
 * Reserve programs the message with MSI and INTx disabled. Enable and Disable
 * toggle MSI delivery. Release restores the original message registers, leaving
 * MSI disabled. INTx returns to the latest accepted bus-control request, or its
 * original state if no request intervened. Stop device activity before
 * Disable/Release.
 * All operations can be retried. A failed write verification keeps the reservation
 * alive. If a response is lost, retry with the same Allocation.
 */
struct PciInterruptRequest {
    uint32_t Operation;
    uuid_t   Allocation;
    uint32_t AddressLow;
    uint32_t AddressHigh;
    uint32_t Data;
    uint32_t Reserved;
};

#endif
