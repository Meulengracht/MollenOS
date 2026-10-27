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
 *
 * MollenOS PCI Bus Driver
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

#ifndef __DEVICED_BUS_PCI_DEVICE_H__
#define __DEVICED_BUS_PCI_DEVICE_H__

#include <bus/pci/bars.h>
#include <bus/pci/msi.h>
#include <bus/pci/msix.h>
#include <bus/pci/registers.h>
#include <ddk/io.h>
#include <ds/list.h>

struct PciFunctionHandler;
struct DmDeviceProviderOperations;

/**
 * @brief Stores a discovered PCI function's bus address, parent and child links,
 * registered I/O regions, interrupt state, and bus attachment.
 */
typedef struct PciDevice {
    element_t            ListHeader;
    element_t            ChildHeader;
    struct PciDevice*    Parent;
    int                  IsBridge;
    PciHost_t*           Host;
    // Registry IDs are distinct from host identity and PCI addresses.
    uuid_t               DeviceId;
    // Changed under the PCI lock. Nonzero references prevent host destruction.
    unsigned int         ProviderReferences;
    // Owns registered BARs, including incomplete publication and cleanup.
    DeviceIo_t           PublishedIo[6];
    struct PciMsiState   Msi;
    struct PciMsixState* Msix;

    struct PciFunctionResources      Resources;
    const struct PciFunctionHandler* Handler;
    void*                            Attachment;
    // Failed attachment uses the saved BAR probe when startup is retried.
    oserr_t                          AttachmentStatus;

    unsigned int Bus;
    unsigned int Slot;
    unsigned int Function;
    unsigned int AcpiConform;
    int          InterruptLine;

    PciNativeHeader_t* Header;
    list_t             Children;
} PciDevice_t;

// Registry callbacks for a PciDevice context. Publication installs these callbacks.
extern const struct DmDeviceProviderOperations g_pciDeviceProviderOperations;

/**
 * @brief Check whether a function may be assigned to a generic driver.
 * Bridges and functions managed by an attached bus are excluded.
 *
 * @param device Discovered PCI function.
 * @return Nonzero for ordinary endpoints with no function handler.
 */
__EXTERN int
PciDeviceAllowsDriverBinding(
    _In_ const PciDevice_t* device);

/**
 * @brief Keep a PCI device, its attachment and host alive for an owner.
 *
 * The caller must keep the device alive through serialized setup or an existing
 * reference before calling. This increments the count checked by host
 * destruction. The reference does not prevent reset or grant hardware access.
 *
 * @param device Live device whose reference count is increased. The caller must
 *               not hold the PCI lock.
 * @return OS_EOK, or OS_EOVERFLOW with no change to ownership.
 */
__EXTERN oserr_t
PciDeviceAddReference(
    _InOut_ PciDevice_t* device);

/**
 * @brief Drop one matching reference without stopping or freeing hardware.
 *
 * The host owner performs destruction later, after all references are gone.
 *
 * @param device Device with a reference held by the caller. The caller must not
 *               hold the PCI lock or use this reference after releasing it.
 */
__EXTERN void
PciDeviceRelease(
    _InOut_ PciDevice_t* device);

#endif // __DEVICED_BUS_PCI_DEVICE_H__
