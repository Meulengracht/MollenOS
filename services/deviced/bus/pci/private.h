/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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
 * Shared PCI host state used while finding devices and releasing hosts.
 * 
 */

#ifndef __DEVICED_PCI_PRIVATE_H__
#define __DEVICED_PCI_PRIVATE_H__

#include "bus.h"

extern list_t g_pciDevices;
extern list_t g_pciRoots;

/**
 * @brief Tracks mapped firmware data while discovery and registered hosts use it.
 * The data is read-only; the reference count is updated during discovery or teardown.
 */
struct PciFirmwareMapping {
    const void*  Blob;
    size_t       Length;
    unsigned int References;
};

/**
 * @brief Releases one user's reference to mapped firmware data. The final release
 * unmaps the data and frees this record.
 * 
 * @param mapping The firmware mapping to release.
 */
__EXTERN void
PciFirmwareRelease(
    _In_ struct PciFirmwareMapping* mapping);


/**
 * @brief Keeps firmware data mapped for a host before registering it. If
 * registration fails, the caller keeps the host and this function drops its
 * temporary reference to the firmware data.
 * 
 * @param host The host being attached.
 * @param mapping The firmware mapping to retain.
 */
__EXTERN oserr_t
PciHostAttach(
    _In_ PciHost_t*                 host,
    _In_ struct PciFirmwareMapping* mapping);

/**
 * @brief Removes the host's device-manager entries, children first. Stop its
 * clients before calling. If removal fails, keep the remaining IDs for a retry.
 * 
 * @param device The host root to unpublish.
 */
__EXTERN oserr_t
PciUnpublishDevice(
    _In_ PciDevice_t* device);

/**
 * @brief Scans a bus for devices and follows any bridges it finds.
 * @param parent Device or host root that owns this bus.
 * @param bus Bus number to scan.
 */
__EXTERN void
PciCheckBus(
    _In_ PciDevice_t* parent,
    _In_ int          bus);

#ifdef __OSCONFIG_HAS_LEGACY_PCI

/**
 * @brief Selects present legacy root functions and scans their buses.
 * 
 * @param host The host to scan for legacy PCI roots.
 */
__EXTERN void
PciScanLegacyRoots(
    _In_ PciHost_t* host);

#endif //!__OSCONFIG_HAS_LEGACY_PCI

#endif //!__DEVICED_PCI_PRIVATE_H__
