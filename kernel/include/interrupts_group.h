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
 * Interrupt groups share lifecycle, and execution control.
 * Kernel callers must supply strategies supported by the device and platform.
 * Syscalls must call InterruptGroupCheckOwner before using a group handle.
 * Call these interfaces outside interrupt (non-syscall) context.
 */

#ifndef __VALI_INTERRUPT_GROUP_H__
#define __VALI_INTERRUPT_GROUP_H__

#include <arch/interrupts.h>
#include <ddk/types/interrupt.h>

/**
 * @brief Reserves a disabled interrupt group without programming PCI hardware.
 *
 * Strategies are tried in MSI-X, MSI, INTx order. MSI and INTx use one vector.
 * MSI-X uses independent vectors. Minimum must be 1..2048 and no greater than
 * Optimal. The allocated count is capped by the backend's capacity.
 *
 * Messages target the x86 BSP. Affinity and CPU hotplug are unsupported.
 * ARM64 does not support message allocation.
 *
 * @param request Requested vector counts, strategies, and interrupt line.
 * @param kernelHandlers 1 for kernel handlers, 0 for bindings in the current memory space.
 * @param result Receives the handle, count, strategy, and first message. Unchanged on failure.
 * @return OS_EOK on success, or an allocation or validation error.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupCreate(
    _In_  const InterruptGroupRequest_t* request,
    _In_  int                           kernelHandlers,
    _Out_ InterruptGroupResult_t*       result);

/**
 * @brief Checks that the current memory space owns a non-kernel group.
 *
 * @param handle Group handle.
 * @return OS_EOK for the owner, OS_EPERMISSIONS for kernel groups or another
 *         owner, or OS_ENOENT if the handle does not exist.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupCheckOwner(
    _In_ uuid_t handle);

/**
 * @brief Binds a handler and its resources to one vector in a disabled group.
 *
 * Finish any pending unbind before binding another handler.
 *
 * @param handle Group handle.
 * @param index Zero-based vector index.
 * @param resources Handler and resources to bind.
 * @return OS_EOK on success, or an error if binding fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupBind(
    _In_ uuid_t                    handle,
    _In_ uint32_t                  index,
    _In_ InterruptResourceTable_t* resources);

/**
 * @brief Reads a vector's hardware message without changing dispatch.
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
 * @brief Enables or disables dispatch for the whole group.
 *
 * Enabling requires all vectors to be bound with no pending unbind.
 * Stop device messages before disabling. Disable waits for running handlers,
 * but cannot mask a PCI source or drain messages in flight.
 *
 * @param handle Group handle.
 * @param enable 1 to enable dispatch, 0 to disable it.
 * @return OS_EOK on success, or an error if the state change fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupSetEnabled(
    _In_ uuid_t handle,
    _In_ int    enable);

/**
 * @brief Removes all handler mappings while keeping the group's reservations.
 *
 * Disable the group first. If cleanup fails, retry before binding or enabling.
 *
 * @param handle Group handle.
 * @return OS_EOK on success, or an error if cleanup fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupUnbind(
    _In_ uuid_t handle);

/**
 * @brief Disables and unbinds the group, then releases its handle.
 *
 * Stop device messages first. Keep the handle and retry if cleanup fails.
 *
 * Non-kernel message vectors that were enabled stay reserved until reboot,
 * disabled and unbound, so a late PCI write cannot reach another device's
 * handler after the group and its mappings are freed.
 * Each retired vector occupies one slot. Disabling and enabling the same
 * group reuses its slots. Reclaiming retired slots requires platform support
 * for draining messages.
 *
 * @param handle Group handle. Invalid after a successful release.
 * @return OS_EOK on success, or an error if cleanup fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptGroupRelease(
    _In_ uuid_t handle);

#endif
