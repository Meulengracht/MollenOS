/**
 * Copyright, Philip Meulengracht
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
 * An interrupt group lets a caller manage related interrupts together. Kernel
 * callers must choose options supported by both the device and the platform.
 * System calls must check ownership before using a group handle. Never call
 * these functions from an interrupt handler.
 */

#ifndef __VALI_INTERRUPT_GROUP_H__
#define __VALI_INTERRUPT_GROUP_H__

#include <interrupt_types.h>
#include <ddk/types/interrupt.h>
#include <ddk/types/interrupt-provider.h>

/**
 * @brief Reserve disabled interrupts without programming PCI hardware.
 *
 * The core tries MSI-X first, then MSI, then INTx. MSI and INTx provide one
 * vector. MSI-X can provide several. Minimum must be between 1 and 2048 and
 * cannot exceed Optimal. The platform may grant fewer than Optimal vectors.
 *
 * ControllerID selects the message controller. User allocations must use an
 * ID found during platform discovery. Kernel tests can use 0 for the platform
 * controller.
 *
 * @param request Requested count, allowed methods, and interrupt line.
 * @param kernelHandlers Nonzero for kernel handlers. Zero binds handlers in the
 *        current process.
 * @param result Receives the group handle, count, selected method, and first
 *        message. The result is unchanged on failure.
 * @return OS_EOK on success, or an allocation or validation error.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupCreate(
    _In_  const InterruptGroupRequest_t* request,
    _In_  int                            kernelHandlers,
    _Out_ InterruptGroupResult_t*        result);

/**
 * @brief Check whether the caller owns a user interrupt group.
 *
 * @param handle Group handle.
 * @return OS_EOK if this process owns the group, OS_EPERMISSIONS if it does
 *         not, or OS_ENOENT if the handle does not exist.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupCheckOwner(
    _In_ uuid_t handle);

/**
 * @brief Attach a handler and its resources to one interrupt in a disabled group.
 *
 * Finish any cleanup from an earlier unbind before attaching another handler.
 * The kernel adds the provider callback already attached to this vector. The
 * device driver supplies only its handler and resources.
 *
 * @param handle Group handle.
 * @param index Zero-based vector index.
 * @param resources Handler and its resources.
 * @return OS_EOK on success, or an error if binding fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupBind(
    _In_ uuid_t                    handle,
    _In_ uint32_t                  index,
    _In_ InterruptResourceTable_t* resources);

/**
 * @brief Read a vector's hardware message without changing its handler.
 *
 * @param handle Group handle.
 * @param index Zero-based vector index.
 * @param message Receives the message address and data. Unchanged on failure.
 * @return OS_EOK on success, OS_ENOTSUPPORTED for INTx, or another error.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupGetMessage(
    _In_  uuid_t              handle,
    _In_  uint32_t            index,
    _Out_ InterruptMessage_t* message);

/**
 * @brief Enable or disable every handler in the group.
 *
 * Bind every vector and finish pending cleanup before enabling the group. Stop
 * device messages before disabling it. Disabling waits for running handlers,
 * but it cannot stop the PCI device or clear messages already in transit.
 *
 * @param handle Group handle.
 * @param enable Set to 1 to enable the handlers or 0 to disable them.
 * @return OS_EOK on success, or an error if the state change fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupSetEnabled(
    _In_ uuid_t handle,
    _In_ int    enable);

/**
 * @brief Remove handler mappings while keeping the group's interrupts reserved.
 *
 * Disable the group first. If cleanup fails, retry before binding handlers or
 * enabling the group. Provider callbacks stay attached, so the next bind uses
 * the same callback.
 *
 * @param handle Group handle.
 * @return OS_EOK on success, or an error if cleanup fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupUnbind(
    _In_ uuid_t handle);

/**
 * @brief Disable the group, remove its handlers, and release it.
 *
 * Stop device messages first. An attached provider must close its attachment
 * before the group can be released. Keep the handle and retry if cleanup fails.
 *
 * After release, enabled user message interrupts stay reserved, disabled, and
 * unbound until reboot. That keeps a late PCI message from reaching another
 * device's handler.
 *
 * @param handle Group handle. It is invalid after a successful release.
 * @return OS_EOK on success, or a cleanup error.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupRelease(
    _In_ uuid_t handle);

/**
 * @brief Reclaim a user group after its owning process has died.
 *
 * Message slots stay reserved because the device may still send interrupts.
 * The bus provider must stop those writes.
 *
 * @param handle Group to reclaim.
 * @return OS_EBUSY for a live owner or concurrent update, OS_ENOENT if absent,
 *         OS_EOK after cleanup, or a retryable cleanup error.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupReap(
    _In_ uuid_t handle);

/**
 * @brief Attach provider callbacks to a disabled, unbound MSI-X group.
 *
 * The group takes one reference to each callback, or none if attachment fails.
 * System calls must check provider permissions before calling this function.
 *
 * @param request Kernel copy of the request and vector array. Principal must be
 *        the driver's identity verified by the provider's message transport.
 *        Each vector receives its reserved hardware message. Driver binds then
 *        use these callbacks without adding them to the driver API.
 * @return OS_EOK or an ownership, validation, or callback availability error.
 *         Retrying the same request returns the same messages without taking
 *         extra callback references. A closed group cannot be attached again.
 */
oserr_t
InterruptGroupProviderAttach(
    _In_ const struct InterruptProviderAttachRequest* request);

/**
 * @brief Check whether the provider's child group is enabled.
 *
 * @param handle Child group handle.
 * @param principal Child process identity verified by the provider.
 * @param device Provider's child device identity.
 * @return OS_EOK if enabled, OS_EBUSY if inactive, or an ownership/lookup error.
 *         This check does not prevent the child from disabling the group later.
 */
oserr_t
InterruptGroupProviderCheckEnabled(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device);

/**
 * @brief Close an attachment after blocking interrupts and unbinding handlers.
 *
 * This drops the group's references to provider callbacks. The provider can
 * release their handles once no other group uses them. System calls must check
 * provider permissions before calling this function.
 *
 * @param handle Child group handle.
 * @param principal Child process identity verified by the provider.
 * @param device Provider's child device identity.
 * @return OS_EOK on closure or an identical retry, OS_EBUSY while bound or
 *         enabled, or an ownership/lookup error. This also closes a group that
 *         was never attached.
 */
oserr_t
InterruptGroupProviderClose(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device);

#endif
