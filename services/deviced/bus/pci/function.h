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
#include <startup.h>

struct PciDevice;
struct DmPublicationGroup;

/**
 * @brief Callbacks and activation policy for a PCI function with child devices.
 *
 * A match keeps a generic driver from taking control of the function, even if Attach fails.
 * When BlockActivation is set, requests to change the function's PCI settings are
 * rejected. Attach must leave its output NULL and free partial state on failure.
 * On success, PCI owns the attachment. Copy resources that must outlive Attach.
 * A transient Attach failure is retried with the saved probe before publication.
 * Unsupported descriptions remain blocked from generic binding and are not retried.
 * Attach records child devices; Publish adds their descriptions after scanning.
 * PrepareHardware readies shared resources after all descriptions are registered,
 * before PCI enables any driver binding. Destroy runs after the group is removed
 * and before host resources are released. A failed Destroy leaves the attachment
 * and its parent alive for retry. Destroy runs under the PCI lock. It must not take
 * that lock again or release references to device operation interfaces in the callback.
 */
struct PciFunctionHandler {
    int BlockActivation;

    /**
     * @brief Check whether this handler supports the PCI function.
     *
     * @param device The PCI device to check for a match.
     * @return Non-zero if the handler matches the device, zero otherwise.
     */
    int (*Match)(
        _In_ const struct PciDevice* device);

    /**
     * @brief Attach a function handler to the given PCI device.
     *
     * @param device The PCI device to attach to. Attachments may keep this parent
     *               pointer so child operation interfaces can later reference it.
     * @param resources Function resources, valid for the duration of this call.
     * @param attachmentOut Receives the attachment owned by PCI, or NULL on failure.
     * @return An error code indicating the result of the attachment.
     */
    oserr_t (*Attach)(
        _In_  struct PciDevice*                  device,
        _In_  const struct PciFunctionResources* resources,
        _Out_ void**                            attachmentOut);

    /**
     * @brief Destroy the attachment associated with the function handler.
     *
     * @param attachment The attachment to destroy.
     * @return OS_EOK after freeing it. On failure, keep the attachment valid.
     *         PCI preserves the parent and host so destruction can be retried.
     */
    oserr_t (*Destroy)(
        _In_ void* attachment);

    /**
     * @brief Block new child activity before removing descriptions, under the PCI lock.
     *
     * @param attachment The attachment owned by PCI.
     * @return OS_EBUSY while clients still need device operation interfaces for cleanup.
     *         On success, block new subscriptions until Destroy or republish.
     */
    oserr_t (*PrepareRemoval)(
        _In_ void* attachment);

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
        _In_ void*                      attachment,
        _In_ const struct PciDevice*     device,
        _In_ struct DmPublicationGroup*  group);

    /**
     * @brief Prepare shared hardware after description publication, under the PCI lock.
     * Called again on publication retries, including after successful binding.
     * An already prepared attachment must leave active resources unchanged.
     *
     * @param attachment The attachment whose children need shared resources.
     * @param result Caller-provided storage. Complete when ready for binding,
     *               retry for unfinished setup or cleanup, or failed for
     *               unsupported hardware. Every failure blocks host binding.
     */
    void (*PrepareHardware)(
        _In_  void*                   attachment,
        _Out_ struct DmStartupResult* result);
};

/**
 * @brief Store the service's function handlers before scanning any devices.
 * Called only by the first PciInitialize. The list and handlers have service lifetime.
 *
 * @param handlers Ordered handler list, or NULL when count is zero.
 * @param count Number of handlers in the list.
 */
__EXTERN void
PciFunctionHandlersInitialize(
    _In_ const struct PciFunctionHandler* const* handlers,
    _In_ size_t                                count);

/**
 * @brief Find a function handler that matches the PCI device.
 *
 * @param device PCI device to match.
 * @return Matching handler, or NULL if no handler accepts the device.
 */
__EXTERN const struct PciFunctionHandler*
PciFunctionHandlerFind(
    _In_ const struct PciDevice* device);

#endif // __DEVICED_BUS_PCI_FUNCTION_H__
