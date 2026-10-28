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

#ifndef __DEVICED_BUS_PCI_PUBLISH_H__
#define __DEVICED_BUS_PCI_PUBLISH_H__

#include <core/publication.h>
#include <startup.h>

struct PciDevice;

/**
 * @brief Tracks publication of one PCI host and its attached child buses.
 * Start with a zeroed structure. Group tracks registry entries and pending
 * driver binding. CleanupPending records unfinished removal or rollback,
 * including attachment removal preparation and published I/O cleanup. It can
 * remain set while Group is empty or has not begun removal.
 * PCI publication owns this structure and changes registry state through the
 * group API.
 */
struct PciPublication {
    struct DmPublicationGroup Group;
    int                       CleanupPending;
};

/**
 * @brief Registers the host root and all PCI and attached child descriptions,
 * then prepares shared hardware before allowing drivers to bind.
 *
 * Complete the host's startup scan successfully before calling. Serialize calls
 * with other publication and host removal operations. Temporary function
 * attachment failures are retried before descriptions are added.
 *
 * If adding an entry fails, the partial group is removed. Repeated calls finish
 * pending rollback before adding descriptions again. Hardware preparation
 * failures keep all descriptions registered and defer binding. A later call
 * retries preparation and binding without restarting successful bindings.
 *
 * @param pciDevice Host root to publish. Child devices are added by this call.
 * @return Complete on success, retry for unfinished resources or temporary failure,
 *         or failed when the current description cannot be published or prepared.
 */
__EXTERN struct DmStartupResult
PciPublishDevice(
    _In_ struct PciDevice* pciDevice);

/**
 * @brief Removes the host's device-manager entries, children first, then destroys
 * published I/O resources.
 *
 * Stop the host's clients and release its PCI interrupt reservations before
 * calling. Active reservations return OS_EBUSY without removing any entries.
 * If removal or I/O destruction fails, keep the host and remaining resources
 * valid for retry. Complete cleanup before republishing.
 *
 * @param device The host root to unpublish.
 * @return OS_EOK when entries and I/O resources are removed, or a cleanup error.
 */
__EXTERN oserr_t
PciUnpublishDevice(
    _In_ struct PciDevice* device);

#endif // __DEVICED_BUS_PCI_PUBLISH_H__
