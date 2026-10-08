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
#include <bus/pci/registers.h>
#include <ddk/io.h>
#include <ds/list.h>

struct PciFunctionHandler;

/**
 * @brief Represents a device on the pci-bus, keeps information
 * about location, children, and a parent device/controller
 */
typedef struct PciDevice {
    element_t         list_header;
    element_t         child_header;
    struct PciDevice* Parent;
    PciHost_t*        Host;
    int               IsBridge;
    // Registry IDs are distinct from host identity and PCI addresses.
    uuid_t            DeviceId;
    // Changed under the PCI lock; nonzero references prevent host destruction.
    unsigned int      ProviderReferences;
    DeviceIo_t        PublishedIo[6];

    struct PciFunctionResources      Resources;
    const struct PciFunctionHandler* Handler;
    void*                            Attachment;

    unsigned int Bus;
    unsigned int Slot;
    unsigned int Function;
    unsigned int AcpiConform;
    int          InterruptLine;

    PciNativeHeader_t* Header;
    list_t             children;
} PciDevice_t;

#endif // __DEVICED_BUS_PCI_DEVICE_H__
