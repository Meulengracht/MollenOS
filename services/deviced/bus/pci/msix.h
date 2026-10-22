/**
 * MollenOS
 *
 * Copyright 2015, Philip Meulengracht
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

#ifndef __PCI_MSIX_H__
#define __PCI_MSIX_H__

#include <bus/pci/bars.h>
#include <bus/pci/msi.h>

struct PciDevice;
struct PciMsixState;

// Represents a validated, CPU-accessible region within one memory BAR.
struct PciMsixRegion {
    uint32_t Bar;
    uint32_t Offset;
    size_t   Length;
    uint64_t Address;
};

// Represents the MSI-X capability and full table/PBA extents.
struct PciMsixLayout {
    uint16_t             Capability;
    uint16_t             Control;
    uint32_t             Count;
    struct PciMsixRegion Table;
    struct PciMsixRegion Pending;
};

// Access operations for MSI-X under the PCI provider lock.
struct MsixDeviceOperations {
    // Configuration and BAR access for the PCI function.
    struct PciConfigOperations Config;
    
    // The enumerated BAR for the MSI-X table and PBA.
    const struct PciBar*       Bars;

    /**
     * @brief Map creates an uncached alias without claiming the driver's BAR
     */
    oserr_t (*Map)(void* context, const struct PciMsixRegion* region, void** mapping);
    
    /**
     * @brief Unmap releases a previously created alias.
     */
    oserr_t (*Unmap)(void* context, void* mapping, size_t length);
    
    /**
     * @brief Read performs an ordered 32-bit read from the mapped region.
     */
    uint32_t (*Read)(void* mapping, size_t offset);
    
    /**
     * @brief Write performs an ordered 32-bit write.
     * Additionally, it reads back the written value for verification.
     */
    void (*Write)(void* mapping, size_t offset, uint32_t value);
};

/**
 * @brief Validate the MSI-X capability and its table and PBA BAR ranges.
 * 
 * @param access Locked configuration access and enumerated BAR descriptions.
 * @param layout Receives validated extents on success. Unchanged on failure.
 * @return OS_EOK, OS_ENOTSUPPORTED if absent, or a validation error.
 */
__EXTERN oserr_t
PciMsixProbe(
    _In_  const struct MsixDeviceOperations* access,
    _Out_ struct PciMsixLayout*              layout);

/**
 * @brief Execute a retryable MSI-X operation while holding the PCI lock.
 * 
 * @param access Locked access to the same function on every call.
 * @param state Owns saved data and mappings until Release succeeds.
 * @param request Fixed-width operation whose allocation identifies its owner.
 * @return OS_EOK on success. On failure, state may remain non-NULL for cleanup.
 */
__EXTERN oserr_t
PciMsixControl(
    _In_    const struct MsixDeviceOperations* access,
    _InOut_ struct PciMsixState**              state,
    _In_    const struct PciInterruptRequest*  request);

/**
 * @brief Remember the latest accepted INTx/MMIO bus-control state for Release.
 * 
 * @param state Active reservation, serialized with control operations.
 * @param command Desired command bits before the provider disables INTx and enables MMIO.
 */
__EXTERN void
PciMsixUpdateCommand(
    _InOut_ struct PciMsixState* state,
    _In_    uint16_t             command);

/**
 * @brief Validate a live device's MSI-X layout without mapping or changing it.
 * 
 * @param device PCI function with published BARs, protected by the PCI lock.
 * @return OS_EOK if the layout can be mapped, or a validation error.
 */
__EXTERN oserr_t
PciMsixDeviceProbe(
    _In_ struct PciDevice* device);

/**
 * @brief Adapt a live device's registered BARs to the MSI-X engine.
 * 
 * @param device PCI function protected by the PCI lock.
 * @param request Operation and allocation owner. See PciInterruptRequest.
 * @return The engine's status. A failed operation can retain cleanup state.
 */
__EXTERN oserr_t
PciMsixDeviceControl(
    _In_ struct PciDevice*                 device,
    _In_ const struct PciInterruptRequest* request);

#endif
