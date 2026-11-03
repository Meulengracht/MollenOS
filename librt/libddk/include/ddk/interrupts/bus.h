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

#ifndef __DDK_INTERRUPT_BUS_H__
#define __DDK_INTERRUPT_BUS_H__

#include <ddk/types/interrupt-bus.h>

// Bus operations must run outside interrupt context. Attaching groups, managing
// bus handlers and reaping groups require INTERRUPT_CONTROL and
// DEVICEIO_MANAGEMENT. Resolving controllers and sources requires only
// INTERRUPT_CONTROL.
//
// The bus configures interrupt delivery and can supply a callback to acknowledge
// upstream hardware. The kernel links it when the driver binds its handler and
// calls it after the device handler returns. Device drivers use the same API
// whether or not the bus needs a callback.
//
// Before enabling hardware delivery, create and bind the callbacks, attach them
// to the driver's group, then wait for the driver to bind and enable its handlers.
// During shutdown, block hardware delivery first. Then disable and unbind the
// handlers and close the attachment before the driver releases its group.
// Release each callback when no group uses it.

_CODE_BEGIN

/**
 * @brief Find the message controller for a firmware identity.
 * The controller ID stays valid until reboot and selects the interrupt route.
 * Device access is checked separately. This call reserves no vectors, creates
 * no bus handler and leaves the hardware unchanged.
 *
 * @param query Platform or PCI host firmware identity.
 * @param idOut Controller ID, unchanged on failure. No close operation is needed.
 * @return OS_EOK, OS_ENOTSUPPORTED or a permission/validation error.
 */
DDKDECL(oserr_t,
DeviceInterruptMessageControllerResolve(
    _In_  const InterruptMessageControllerIdentity_t* query,
    _Out_ uuid_t*                                     idOut));

/**
 * @brief Verify a firmware line source against the active kernel controller.
 * Requires INTERRUPT_CONTROL. The returned ID selects the controller. Device
 * access is checked separately, and this call leaves the source unchanged.
 * Only ARM GIC SPIs are currently supported. If the architecture is unsupported
 * or the controller and line do not match, the call fails without choosing
 * another route.
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
 * @brief Create an empty bus handler for the calling process.
 *
 * Create the handle before binding so the bus can retry cleanup if mapping
 * fails. Creation reserves no interrupt and maps no resources. Any thread in
 * the bus process can use the handle. Keep it until
 * DeviceInterruptBusHandlerRelease succeeds.
 *
 * @param handleOut Receives the bus handler handle on success.
 * @return OS_EOK on success, or a permission, allocation or copy error.
 */
DDKDECL(oserr_t,
DeviceInterruptBusHandlerCreate(
    _Out_ uuid_t* handleOut));

/**
 * @brief Bind a fast callback and its resources to an empty bus handler.
 *
 * Keep the callback code, resources and handle valid until
 * DeviceInterruptBusHandlerRelease succeeds. Binding does not attach a group
 * or enable interrupt delivery. Once attached, the callback runs
 * after the device handler, even when that handler reports no work. Its return
 * value is ignored.
 *
 * @param handle Bus handler handle created by the calling process.
 * @param resources Callback and resources in the bus process's address space. The
 *        callback runs in the kernel, so it can use only these mapped resources
 *        and the supplied function table. It may run on several CPUs at once.
 *        Protect shared data without waiting for a normal thread to run.
 * @return OS_EOK on success, or a validation, permission or mapping error.
 *         If binding fails, keep the handle and call
 *         DeviceInterruptBusHandlerRelease until cleanup succeeds.
 */
DDKDECL(oserr_t,
DeviceInterruptBusHandlerBind(
    _In_ uuid_t                          handle,
    _In_ const InterruptResourceTable_t* resources));

/**
 * @brief Release a bus handler that is not attached to a group.
 * Close or reap all attached groups first, including disabled groups.
 * Releasing the callback does not close groups or stop the device hardware.
 *
 * @param handle Bus handler handle created by the calling process.
 * @return OS_EOK on release, OS_EBUSY while attached or updating, OS_ENOENT if
 *         absent, or a permission or cleanup error. Keep the handle and retry
 *         if cleanup fails.
 */
DDKDECL(oserr_t,
DeviceInterruptBusHandlerRelease(
    _In_ uuid_t handle));

/**
 * @brief Attach bus callbacks to a disabled, unbound device interrupt group.
 *
 * Before the first attachment, the group must be disabled and unbound. Each
 * message vector can have one callback, and several vectors can share it.
 * Validated native fixed-line groups have one source, no callback and no
 * message. Line, LineFlags and the controller must match the allocation exactly.
 * Legacy and manually configured line groups cannot be attached.
 *
 * An identical retry returns the existing messages even after handlers have
 * been bound or enabled. It cannot replace their callbacks. The kernel records
 * this bus process and device until the group is closed.
 *
 * @param request Allocation, verified driver identity, device identity,
 *        controller, and vector array. The kernel writes each chosen message
 *        address and data to the vector's Message field.
 * @return OS_EOK on attachment or an identical retry, or a validation, permission,
 *         allocation, or copy error. A copy back to user memory can fail after
 *         attachment succeeds. Retry the same request to retrieve the messages,
 *         or close the attachment before discarding it. Keep hardware delivery
 *         blocked until the driver has bound and enabled every handler.
 */
DDKDECL(oserr_t,
DeviceInterruptBusAttach(
    _In_ const InterruptBusAttachRequest_t* request));

/**
 * @brief Check whether an attached child group is enabled.
 * The result describes the group's state at the time of the check. The bus
 * must still synchronize device setup and shutdown before enabling hardware.
 *
 * @param handle Allocation handle supplied by the child.
 * @param principal Driver identity verified by the message transport.
 * @param device Child device identity used when attaching the group.
 * @return OS_EOK if enabled, OS_EBUSY if inactive, or a permission/lookup error.
 *         This check does not prevent the child from disabling the group later.
 */
DDKDECL(oserr_t,
DeviceInterruptBusStatus(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device));

/**
 * @brief Close an attachment after blocking device interrupts and unbinding handlers.
 * Drops this group's callback references and permanently prevents new binds
 * and attachments. It does not release the group's vector reservation or the
 * bus handler handles. A successful driver disable call also unbinds handlers.
 * If closing fails, keep the attachment details and retry after cleanup.
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
DeviceInterruptBusClose(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device));

/**
 * @brief Clean up an interrupt allocation after its driver process dies.
 * Stop bus delivery before reaping. If cleanup leaves part of the group behind,
 * keep the allocation handle and retry from any bus thread. The kernel checks
 * that the process that created the group has exited.
 *
 * @param handle Allocation handle to clean up.
 * @return OS_EOK after cleanup, OS_EBUSY for a live owner or concurrent
 *         update, OS_ENOENT if absent, or a permission/retryable cleanup error.
 *         Message slots stay reserved until reboot. This keeps delayed device
 *         writes from reaching another device's handler. Reaping does not stop
 *         the device or release the bus handler handles.
 */
DDKDECL(oserr_t,
DeviceInterruptReap(
    _In_ uuid_t handle));

_CODE_END

#endif //!__DDK_INTERRUPT_BUS_H__
