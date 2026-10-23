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
 * Interrupt groups let a caller manage several related interrupts together.
 * Kernel callers must request options supported by both the device and platform.
 * System calls must check group ownership before using a handle.
 * Do not call these functions from an interrupt handler.
 */

#ifndef __VALI_INTERRUPT_GROUP_H__
#define __VALI_INTERRUPT_GROUP_H__

#include <interrupt_types.h>
#include <ddk/types/interrupt.h>

/**
 * @brief Reserve a group of disabled interrupts without programming PCI hardware.
 *
 * The core tries MSI-X, then MSI, then INTx. MSI and INTx provide one interrupt
 * vector. MSI-X can provide several independent vectors. Minimum must be from
 * 1 to 2048 and no greater than Optimal. The platform may provide fewer than
 * Optimal requests.
 *
 * ControllerID selects the message controller. User allocations must provide
 * an ID found during platform discovery. Kernel tests may use 0 to select the
 * platform controller.
 *
 * @param request Requested interrupt count, allowed methods, and interrupt line.
 * @param kernelHandlers Nonzero for kernel handlers. Zero binds handlers in
 *        the current process.
 * @param result Receives the group handle, count, selected method, and first
 *        message. Unchanged on failure.
 * @return OS_EOK on success, or an allocation or validation error.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupCreate(
    _In_  const InterruptGroupRequest_t* request,
    _In_  int                            kernelHandlers,
    _Out_ InterruptGroupResult_t*        result);

/**
 * @brief Check that the caller's memory space owns a user interrupt group.
 *
 * @param handle Group handle to check.
 * @return OS_EOK if this process owns the group, OS_EPERMISSIONS if it does
 *         not, or OS_ENOENT if the handle does not exist.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupCheckOwner(
    _In_ uuid_t handle);

/**
 * @brief Attach a handler and its resources to one interrupt in a disabled group.
 *
 * Complete cleanup from an earlier unbind before attaching another handler.
 *
 * @param handle Group handle.
 * @param index Zero-based interrupt index.
 * @param resources Handler and resources to attach.
 * @return OS_EOK on success, or an error if binding fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupBind(
    _In_ uuid_t                    handle,
    _In_ uint32_t                  index,
    _In_ InterruptResourceTable_t* resources);

/**
 * @brief Read one interrupt's hardware message without changing its handler.
 *
 * @param handle Group handle.
 * @param index Zero-based interrupt index.
 * @param message Receives the message address and data. Unchanged on failure.
 * @return OS_EOK on success, OS_ENOTSUPPORTED for INTx, or another error.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupGetMessage(
    _In_  uuid_t              handle,
    _In_  uint32_t            index,
    _Out_ InterruptMessage_t* message);

/**
 * @brief Enable or disable all handlers in the group.
 *
 * Bind every interrupt and finish any pending cleanup before enabling. Stop
 * device messages before disabling. Disabling waits for running handlers, but
 * cannot stop the PCI device or clear messages already in transit.
 *
 * @param handle Group handle.
 * @param enable 1 to enable the handlers, 0 to disable them.
 * @return OS_EOK on success, or an error if the state change fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupSetEnabled(
    _In_ uuid_t handle,
    _In_ int    enable);

/**
 * @brief Remove all handler resource mappings but keep the group's interrupts reserved.
 *
 * Disable the group first. If cleanup fails, retry before attaching handlers
 * or enabling the group.
 *
 * @param handle Group handle.
 * @return OS_EOK on success, or an error if cleanup fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupUnbind(
    _In_ uuid_t handle);

/**
 * @brief Disable the group, remove its handlers, and release its handle.
 *
 * Stop device messages first. Keep the handle and retry if cleanup fails.
 *
 * User message interrupts that were enabled stay reserved, disabled and
 * unbound, until reboot after release. This keeps a late PCI message from
 * reaching another device's handler after the group and its mappings are gone.
 *
 * @param handle Group handle. Invalid after a successful release.
 * @return OS_EOK on success, or an error if cleanup fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupRelease(
    _In_ uuid_t handle);

#endif
