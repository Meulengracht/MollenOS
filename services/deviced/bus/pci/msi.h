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
 * Single-message PCI MSI configuration and reservation handling.
 */

#ifndef __PCI_MSI_H__
#define __PCI_MSI_H__

#include <ddk/types/pci-interrupt.h>
#include <ddk/types/interrupt.h>

// Callbacks for reading and writing PCI configuration space.
struct PciConfigOperations {
    void*     Context;
    uint32_t (*Read)(void* context, size_t offset, size_t width);
    void     (*Write)(void* context, size_t offset, uint32_t value, size_t width);
};

// MSI and MSI-X capabilities advertised by a PCI function.
struct PciInterruptCapabilities {
    uint16_t MsiOffset;
    uint32_t MsiCapacity;

    uint16_t MsixOffset;
    uint32_t MsixCapacity;
};

/**
 * @brief Saved PCI configuration for one MSI reservation.
 *
 * Saved fields stay unchanged across retries, except Command's INTx-disable
 * bit. Bus control can update that bit under the same lock. Release restores
 * the latest accepted value, or the original value if no update occurred.
 */
struct PciMsiState {
    uuid_t   Allocation;
    int      Active;
    int      Prepared;
    int      Supplied;
    int      Programmed;
    int      MayDeliver;
    int      Releasing;

    // Saved MSI capability and control registers.
    uint16_t Capability;
    uint16_t Control;
    uint16_t Command;
    uint16_t Data;
    uint32_t AddressLow;
    uint32_t AddressHigh;
    uint32_t Mask;
    
    // Interrupt message used by this reservation.
    DeviceInterruptMessage_t Message;
};

/**
 * @brief Inspect disabled MSI and MSI-X capabilities without writing registers.
 *
 * Rejects malformed chains, duplicate capabilities, and enabled message modes.
 * The caller holds the PCI lock. The output stays unchanged on failure.
 *
 * @param access Configuration access for one endpoint.
 * @param capabilities Receives capability offsets and hardware capacities.
 * @return OS_EOK if either capability exists, OS_ENOTSUPPORTED if neither does,
 *         or an error for malformed configuration or an already enabled mode.
 */
__EXTERN oserr_t
PciInterruptProbe(
    _In_  const struct PciConfigOperations* access,
    _Out_ struct PciInterruptCapabilities*  capabilities);

/**
 * @brief Validate the shared MSI/MSI-X request fields before touching hardware.
 *
 * @param request Request with one message strategy and zeroed unused fields.
 * @return Nonzero when the request matches the wire contract.
 */
__EXTERN int
PciInterruptRequestIsValid(
    _In_ const struct PciInterruptRequest* request);

/**
 * @brief Walk the conventional capability chain and find a disabled MSI capability.
 *
 * Rejects cycles, misaligned or out-of-range links, duplicate MSI capabilities,
 * and truncated layouts. The function must be an endpoint with MSI and MSI-X
 * disabled. This probe writes no registers. The caller checks platform MSI policy.
 */
__EXTERN oserr_t
PciMsiProbe(
    _In_  const struct PciConfigOperations* access,
    _Out_ uint16_t*                         capability);

/**
 * @brief Reserve, program, enable, disable or release MSI under the caller's lock.
 *
 * Stop the device before disabling or releasing MSI. A failed Reserve can leave
 * Active set, so call Release before freeing the kernel allocation. Disable and
 * Release return OS_EOK when the reservation is already gone, making retries
 * safe after a lost service response.
 *
 * OS_EBUSY means another reservation owns the function, MSI or MSI-X was
 * already enabled, or Enable came before preparation finished. OS_EUNKNOWN
 * means a write did not pass readback. This function does not check device access.
 */
__EXTERN oserr_t
PciMsiControl(
    _In_    const struct PciConfigOperations* access,
    _InOut_ struct PciMsiState*               state,
    _In_    const struct PciInterruptRequest* request);

#endif
