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

#include <ddk/types/interrupt-pci.h>
#include <ddk/interrupts/interrupt.h>

/**
 * @brief Allocate PCI interrupt vectors using modes supported by the driver.
 * Requires an initialized PCI interrupt object. Modes are tried in MSI-X, MSI,
 * INTx order. The next mode is tried only if a mode is unsupported or the kernel
 * cannot supply enough vectors. PCI setup and transport errors follow the usual
 * cleanup rules. Non-PCI objects are rejected.
 * MSI and INTx require min == 1. For MSI-X, the count is between min and optimal,
 * limited by the table size and available kernel vectors. Multi-message MSI is
 * not implemented. The interrupt flags accept only INTERRUPT_EXCLUSIVE.
 *
 * PCI message delivery stays masked during allocation. Before Enable, configure
 * handlers with the DeviceInterrupt setters. Use Enable, Disable and Destroy to
 * control delivery and release the vectors. For MSI-X, configure device queues
 * and interrupt causes for table entries 0 through countOut - 1 before Enable.
 * Keep device interrupt sources stopped during setup and teardown. If failed
 * setup leaves State set, retry Destroy until cleanup succeeds before reusing
 * or discarding the object.
 * 
 * @param interrupt Initialized PCI device interrupt object.
 * @param min Minimum acceptable vector count.
 * @param optimal Preferred count. MSI and INTx always provide one vector.
 * @param modes Nonzero bitmask of INTERRUPT_STRATEGY_* modes supported by the driver.
 * @param countOut Actual count, or zero on failure.
 * @return OS_EOK, OS_ENOTSUPPORTED when no permitted mode meets the request,
 *         OS_EBUSY if an allocation still exists or is busy, or a validation
 *         or setup error.
 */
DDKDECL(oserr_t,
DeviceInterruptAllocatePci(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _In_    uint32_t           modes,
    _Out_   uint32_t*          countOut));

/**
 * @brief Read the selected PCI mode for device queue and interrupt cause setup.
 * Requires an initialized interrupt object. Reading the mode leaves delivery
 * unchanged and does not indicate that the interrupt is ready. The mode may
 * still be available after teardown fails and needs to be retried.
 *
 * @param interrupt Non-NULL initialized interrupt object.
 * @return One INTERRUPT_STRATEGY_* value, or zero for no PCI allocation.
 */
DDKDECL(uint32_t,
DeviceInterruptGetPciMode(
    _In_ const DeviceInterrupt_t* interrupt));

#endif //!__DDK_PCI_INTERRUPT_H__
