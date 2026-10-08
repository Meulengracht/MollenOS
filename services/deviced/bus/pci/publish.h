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

#include <os/osdefs.h>

struct PciDevice;

/**
 * @brief Adds the host root and all PCI and attached child descriptions before
 * allowing drivers to match them. Adding an entry may fail; the partial group
 * is then removed. Binding failures retain the complete group for retry.
 *
 * @param pciDevice Host root to publish. Child devices are added by this call.
 * @return OS_EOK on success, or an error adding, binding, or removing entries.
 */
__EXTERN oserr_t
PciPublishDevice(
    _In_ struct PciDevice* pciDevice);

/**
 * @brief Removes the host's device-manager entries, children first. Stop its
 * clients before calling. If removal fails, keep the remaining IDs for a retry.
 *
 * @param device The host root to unpublish.
 */
__EXTERN oserr_t
PciUnpublishDevice(
    _In_ struct PciDevice* device);

#endif // __DEVICED_BUS_PCI_PUBLISH_H__
