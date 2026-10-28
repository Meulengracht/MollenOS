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

struct PciMsixRegion {
    uint32_t Bar;
    uint32_t Offset;
    size_t   Length;
    uint64_t Address;
};

struct PciMsixLayout {
    uint16_t             Capability;
    uint16_t             Control;
    uint32_t             Count;
    struct PciMsixRegion Table;
    struct PciMsixRegion Pending;
};

/**
 * @brief Configuration, BAR, and mapping operations used with the PCI lock held.
 */
struct MsixDeviceOperations {
    // Configuration and BAR access for the PCI function.
    struct PciConfigOperations Config;

    // Enumerated BAR descriptions indexed by the capability's BAR numbers.
    const struct PciBar*       Bars;

    /**
     * @brief Create an uncached mapping while the driver keeps ownership of its BAR.
     *
     * @param context Configuration access context from Config.Context.
     * @param region Validated BAR range to map.
     * @param mapping Receives the mapped address on success.
     * @return OS_EOK on success, or the mapping error.
     */
    oserr_t (*Map)(
        _In_  void*                       context,
        _In_  const struct PciMsixRegion*  region,
        _Out_ void**                      mapping);

    /**
     * @brief Release a mapping created by Map.
     *
     * @param context Configuration access context from Config.Context.
     * @param mapping Address returned by Map.
     * @param length Original region length in bytes.
     * @return OS_EOK on success, or an error that allows cleanup to be retried.
     */
    oserr_t (*Unmap)(
        _In_ void*  context,
        _In_ void*  mapping,
        _In_ size_t length);

    /**
     * @brief Perform an ordered 32-bit read from the mapped region.
     *
     * @param mapping Address returned by Map.
     * @param offset Byte offset of a complete, aligned 32-bit word in the region.
     * @return The value read from the device.
     */
    uint32_t (*Read)(
        _In_ void*  mapping,
        _In_ size_t offset);

    /**
     * @brief Perform an ordered 32-bit write. The caller reads back to verify it.
     *
     * @param mapping Address returned by Map.
     * @param offset Byte offset of a complete, aligned 32-bit word in the region.
     * @param value Value to write to the device.
     */
    void (*Write)(
        _In_ void*    mapping,
        _In_ size_t   offset,
        _In_ uint32_t value);
};

/**
 * @brief Validate the MSI-X capability and its table and pending-bit array (PBA).
 *
 * @param access Locked configuration access and enumerated BAR descriptions.
 * @param layout Receives validated BAR ranges on success. Unchanged on failure.
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
 * @param command Desired command bits before MSI-X disables INTx and enables MMIO.
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
 * @return The engine's status. A failed operation can leave cleanup state for retry.
 */
__EXTERN oserr_t
PciMsixDeviceControl(
    _In_ struct PciDevice*                 device,
    _In_ const struct PciInterruptRequest* request);

#endif
