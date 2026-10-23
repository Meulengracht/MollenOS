/**
 * Copyright 2026, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DDK_INTERRUPT_PROVIDER_INTERFACE_H__
#define __DDK_INTERRUPT_PROVIDER_INTERFACE_H__

#include <ddk/types/interrupt-provider.h>

// These operations require INTERRUPT_CONTROL and DEVICEIO_MANAGEMENT. They run
// outside interrupt context and are for bus providers, not ordinary device
// drivers. The provider configures interrupt delivery and can supply a callback
// to acknowledge upstream hardware after the device handler runs. The kernel
// invokes that callback when the driver binds its handler, so the driver API
// stays the same.
//
// Before enabling hardware delivery, create and bind the callbacks, attach them
// to the driver's group, then wait for the driver to bind and enable its handlers.
// During shutdown, block hardware delivery first. Then disable and unbind the
// handlers and close the attachment. The driver can release its group after
// that. Release each callback when no group uses it.

_CODE_BEGIN

/**
 * @brief Create an empty completion for the calling provider process.
 *
 * Create the handle before binding. If mapping fails, the provider can still
 * retry cleanup through that handle. This does not reserve an interrupt.
 *
 * @param handleOut Receives the completion handle on success.
 * @return OS_EOK on success, or a permission, allocation or copy error.
 */
DDKDECL(oserr_t,
DeviceInterruptCompletionCreate(
    _Out_ uuid_t* handleOut));

/**
 * @brief Bind a fast callback and its resources to an empty completion.
 *
 * @param handle Completion handle owned by the caller.
 * @param resources Callback and resources in the provider's address space. The
 *        callback runs in the kernel, so it can use only these mapped resources
 *        and the supplied function table. It may run on several CPUs at once.
 *        Protect shared data without waiting for a normal thread to run.
 * @return OS_EOK on success, or a validation, ownership or mapping error.
 *         If binding fails, keep the handle and call Release until cleanup
 *         succeeds. Binding alone does not attach the callback to a vector.
 */
DDKDECL(oserr_t,
DeviceInterruptCompletionBind(
    _In_ uuid_t                          handle,
    _In_ const InterruptResourceTable_t* resources));

/**
 * @brief Release a completion that is not attached to a group.
 *
 * @param handle Completion handle owned by the caller.
 * @return OS_EOK on release, OS_EBUSY while attached or updating, OS_ENOENT if
 *         absent, or a permission or cleanup error. Keep the handle and retry
 *         if cleanup fails.
 */
DDKDECL(oserr_t,
DeviceInterruptCompletionRelease(
    _In_ uuid_t handle));

/**
 * @brief Attach provider callbacks to a disabled, unbound device interrupt group.
 *
 * Each vector can have one completion, and several vectors can share one.
 * This API supports MSI-X groups only.
 *
 * @param request Allocation, verified driver identity, device identity,
 *        controller, and vector array. The kernel writes each chosen message
 *        address and data to the vector's Message field.
 * @return OS_EOK on attachment or an exact retry, or a validation, ownership,
 *         allocation, or copy error. A copy back to user memory can fail after
 *         attachment succeeds. Retry the same request to retrieve the messages,
 *         or close the attachment before discarding it. Keep hardware delivery
 *         blocked until the driver has bound and enabled every handler.
 */
DDKDECL(oserr_t,
DeviceInterruptProviderAttach(
    _In_ const struct InterruptProviderAttachRequest* request));

/**
 * @brief Check whether an attached child group is enabled.
 *
 * @param handle Allocation handle supplied by the child.
 * @param principal Driver identity verified by the message transport.
 * @param device Child device identity used when attaching the group.
 * @return OS_EOK if enabled, OS_EBUSY if inactive, or a permission/lookup error.
 *         This check does not prevent the child from disabling the group later.
 */
DDKDECL(oserr_t,
DeviceInterruptProviderCheckEnabled(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device));

/**
 * @brief Close an attachment after blocking device interrupts and unbinding handlers.
 *
 * @param handle Allocation handle supplied by the child.
 * @param principal Driver identity verified by the message transport.
 * @param device Child device identity used when attaching the group.
 * @return OS_EOK on closure or an identical retry, OS_EBUSY while bound/enabled,
 *         or a permission or lookup error. This also closes a group that was
 *         never attached, blocking any delayed attachment. The child releases
 *         its allocation separately.
 */
DDKDECL(oserr_t,
DeviceInterruptProviderClose(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device));

/**
 * @brief Clean up an interrupt allocation after its driver process dies.
 *
 * @param handle Allocation handle to clean up.
 * @return OS_EOK after cleanup, OS_EBUSY for a live owner or concurrent
 *         update, OS_ENOENT if absent, or a permission/retryable cleanup error.
 *         Message slots stay reserved until reboot. This keeps delayed device
 *         writes from reaching another device's handler. Reaping does not stop
 *         the device or release the provider's completions.
 */
DDKDECL(oserr_t,
DeviceInterruptReap(
    _In_ uuid_t handle));

_CODE_END

#endif
