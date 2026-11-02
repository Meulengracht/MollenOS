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

#include <ddk/types/interrupt-bus.h>

// Attachment, completion and reap operations require INTERRUPT_CONTROL and
// DEVICEIO_MANAGEMENT. Read-only controller/source resolution requires only
// INTERRUPT_CONTROL. These operations run outside interrupt context and are for
// bus providers, not ordinary device
// drivers. The provider configures interrupt delivery and can supply a callback
// to acknowledge upstream hardware after the device handler runs. The kernel
// links that callback when the driver binds its handler and invokes it after
// the device handler runs, so the driver API stays the same.
//
// Before enabling hardware delivery, create and bind the callbacks, attach them
// to the driver's group, then wait for the driver to bind and enable its handlers.
// During shutdown, block hardware delivery first. Then disable and unbind the
// handlers and close the attachment. The driver can release its group after
// that. Release each callback when no group uses it.

_CODE_BEGIN

/**
 * @brief Resolve a boot-lifetime message allocator without reserving vectors.
 * The ID is a routing selector, not authorization to access a device. No
 * completion, hardware programming or allocation ownership is created.
 *
 * @param query Platform or PCI host firmware identity.
 * @param idOut Controller ID, unchanged on failure; no close operation needed.
 * @return OS_EOK, OS_ENOTSUPPORTED or a permission/validation error.
 */
DDKDECL(oserr_t,
DeviceInterruptMessageControllerResolve(
    _In_  const InterruptMessageControllerIdentity_t* query,
    _Out_ uuid_t*                                     idOut));

/**
 * @brief Verify a firmware line source against the active kernel controller.
 * Resolution is read-only and requires INTERRUPT_CONTROL. The returned ID is
 * a controller selector, not device authorization or an activation lease.
 * Current native fixed sources are ARM GIC SPIs; unsupported architectures or
 * mismatched controller/line identities are rejected without a fallback route.
 *
 * @param query DT controller phandle and fixed interrupt number.
 * @param idOut Kernel controller ID, unchanged on failure.
 * @return OS_EOK, OS_ENOTSUPPORTED or a permission/validation error.
 */
DDKDECL(oserr_t,
DeviceInterruptLineSourceResolve(
    _In_  const InterruptLineSourceIdentity_t* query,
    _Out_ uuid_t*                              idOut));


/**
 * @brief Create an empty completion for the calling provider process.
 *
 * Create the handle before binding. If mapping fails, the provider can still
 * retry cleanup through that handle. This does not reserve an interrupt.
 * The handle belongs to the provider process, not a worker thread; subsequent
 * operations may run on another thread in the same process. Keep the handle
 * until CompletionRelease succeeds. CompletionCreate itself installs no mapping.
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
 * Binding does not attach any group or enable delivery. The provider retains
 * its backing code, resources and handle through all group attachments and
 * until CompletionRelease succeeds. The callback runs after the device handler
 * even when that handler reports no work; its return value is ignored.
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
 * Any attached group reference prevents release, including while that group
 * is disabled. Close or reap the groups first; releasing a completion does not
 * close groups or stop device hardware on the provider's behalf.
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
 * Message groups may carry callbacks. Explicitly validated native fixed-line
 * groups have one source, no callback and no message; Line/LineFlags and the
 * controller must match the allocation exactly. Legacy/manual lines are not attachable.
 * Keep the first attachment disabled and unbound. An exact retry may return its
 * existing messages after binding/enabling; it cannot replace running callbacks.
 * The kernel records this provider process and device until terminal closure.
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
    _In_ const InterruptBusAttachRequest_t* request));

/**
 * @brief Check whether an attached child group is enabled.
 * A successful result is a current-state check only. The provider must retain
 * its own serialization and device lifecycle ordering before enabling hardware.
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
 * Drops this group's callback references and permanently prevents new binds
 * and attachments. It does not release the group's vector reservation or the
 * provider's completion handles. Successful driver Disable includes unbinding.
 * Keep the attachment/cleanup record if closure fails and retry after cleanup.
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
 * Stop provider delivery before reaping. A cleanup error can leave a partially
 * removed group; preserve the allocation token and retry from any provider
 * worker. The kernel verifies that the owning process is no longer alive.
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
