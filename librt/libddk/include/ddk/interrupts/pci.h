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

#ifndef __DDK_PCI_INTERRUPT_H__
#define __DDK_PCI_INTERRUPT_H__

#include <ddk/types/pci-interrupt.h>
#include <ddk/interrupts/interrupt.h>

/**
 * @brief Reserve using only the PCI modes the driver knows how to configure.
 * Modes are tried in MSI-X, MSI, INTx order. Only unsupported modes and kernel
 * vector exhaustion permit fallback. PCI setup/transport errors retain normal
 * lifecycle cleanup semantics. Non-PCI objects are rejected.
 * MSI and INTx require min == 1. MSI-X can negotiate a count from min to optimal,
 * bounded by table size and available kernel vectors. Multi-message MSI is not
 * implemented. Flags on the interrupt object accept only INTERRUPT_EXCLUSIVE.
 *
 * Allocation prepares PCI message delivery while masked; bind/configure handlers
 * with the shared DeviceInterrupt setters, then use its Enable/Disable/Destroy
 * lifecycle. For MSI-X, configure device queues/causes for table entries
 * 0 through countOut - 1 before Enable. Keep device interrupt sources stopped
 * during setup and teardown. Failed setup can retain State; retry Destroy until
 * cleanup succeeds before reusing or discarding the object.
 * 
 * @param interrupt Initialized PCI device interrupt object.
 * @param min Minimum acceptable vector count.
 * @param optimal Preferred count; MSI and INTx always provide one vector.
 * @param modes Nonzero bitmask of INTERRUPT_STRATEGY_* modes supported by the driver.
 * @param countOut Actual count, or zero on failure.
 * @return OS_EOK, OS_ENOTSUPPORTED when no permitted mode meets the request,
 *         OS_EBUSY for a retained/busy allocation, or a validation/setup error.
 */
DDKDECL(oserr_t,
DeviceInterruptAllocatePci(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _In_    uint32_t           modes,
    _Out_   uint32_t*          countOut));

/**
 * @brief Read the selected PCI mode for device queue/cause configuration.
 * This is a query only; it neither changes delivery nor establishes readiness.
 * A retained allocation may still expose its mode while teardown needs retry.
 * @param interrupt Non-NULL initialized interrupt object.
 * @return One INTERRUPT_STRATEGY_* value, or zero for no PCI allocation.
 */
DDKDECL(uint32_t,
DeviceInterruptGetPciMode(
    _In_ const DeviceInterrupt_t* interrupt));

#endif //!__DDK_PCI_INTERRUPT_H__
