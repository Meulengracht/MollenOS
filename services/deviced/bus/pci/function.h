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

#ifndef __DEVICED_BUS_PCI_FUNCTION_H__
#define __DEVICED_BUS_PCI_FUNCTION_H__

#include <bus/pci/bars.h>

struct PciDevice;
struct DmPublicationGroup;

/**
 * @brief Handles a PCI function that has child devices of its own.
 * A match keeps a generic driver from claiming the function, even if Attach fails.
 * When BlockActivation is set, requests to change the function's PCI settings are
 * rejected. Attach must leave its output NULL and free partial state on failure;
 * on success, PCI owns the attachment. Copy resources that must outlive Attach.
 * Attach records child devices; Publish adds them after scanning, without starting
 * their drivers. Destroy runs after the publication group is removed and before
 * host resources are released. A failed Destroy leaves the attachment and its
 * parent alive for retry. Destroy runs under the PCI lock; it must not acquire
 * that lock again or release provider references from inside the callback.
 */
struct PciFunctionHandler {
    int BlockActivation;

    /**
     * @brief Determine if a function handler exists for this PCI device.
     *
     * @param device The PCI device to check for a match.
     * @return Non-zero if the handler matches the device, zero otherwise.
     */
    int (*Match)(
        const struct PciDevice* device);

    /**
     * @brief Attach a function handler to the given PCI device.
     *
     * @param device The PCI device to attach to. Attachments may keep this owner
     *               pointer so their later providers can retain the exact device.
     * @param resources The resources allocated for the function.
     * @param attachmentOut Output parameter for the attachment.
     * @return An error code indicating the result of the attachment.
     */
    oserr_t (*Attach)(
        struct PciDevice*                  device,
        const struct PciFunctionResources* resources,
        void**                             attachmentOut);

    /**
     * @brief Destroy the attachment associated with the function handler.
     *
     * @param attachment The attachment to destroy.
     * @return OS_EOK after freeing it. On failure, keep the attachment valid;
     *         PCI preserves the parent and host so destruction can be retried.
     */
    oserr_t (*Destroy)(void* attachment);

    /**
     * @brief Adds child descriptions to the host's group beneath this function.
     * The group handles driver matching and removal, including partial failure.
     *
     * @param attachment The attachment associated with the function handler.
     * @param device The already registered parent PCI function.
     * @param group The host's group, still accepting descriptions.
     * @return OS_EOK on success, or the first error adding a child.
     */
    oserr_t (*Publish)(
        void*                      attachment,
        const struct PciDevice*    device,
        struct DmPublicationGroup* group);
};

/**
 * @brief Check if we have a function-handler registered for the pci device.
 */
__EXTERN const struct PciFunctionHandler*
PciFunctionHandlerFind(
    _In_ const struct PciDevice* device);

#endif // __DEVICED_BUS_PCI_FUNCTION_H__
