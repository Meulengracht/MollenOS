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

#include <ddk/interrupts/bus.h>
#include <internal/_syscalls.h>

oserr_t
DeviceInterruptBusHandlerCreate(
    _Out_ uuid_t* handleOut)
{
    // Create the handle before binding so the bus can retry cleanup if mapping
    // fails. The kernel writes the handle directly to the caller's buffer.
    return Syscall_InterruptBusHandlerCreate(handleOut);
}

oserr_t
DeviceInterruptBusHandlerBind(
    _In_ uuid_t                          handle,
    _In_ const InterruptResourceTable_t* resources)
{
    // The kernel copies and validates this description before mapping resources.
    // Return its error unchanged so the bus can handle any required cleanup.
    return Syscall_InterruptBusHandlerBind(handle, resources);
}

oserr_t
DeviceInterruptBusHandlerRelease(
    _In_ uuid_t handle)
{
    // The kernel checks whether any group still uses this callback. If release
    // fails, the bus must keep the handle and retry.
    return Syscall_InterruptBusHandlerRelease(handle);
}

oserr_t
DeviceInterruptBusAttach(
    _In_ const InterruptBusAttachRequest_t* request)
{
    // Attach all vectors in one call. If copying the result fails after
    // attachment, the bus can retry the same request to get the messages.
    return Syscall_InterruptBusAttach(request);
}

oserr_t
DeviceInterruptBusStatus(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    // Read the state from the kernel each time. The driver can enable or disable
    // its group without going through the bus.
    return Syscall_InterruptBusStatus(handle, principal, device);
}

oserr_t
DeviceInterruptBusClose(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    // Closing drops this group's callback references. The bus keeps the handles,
    // as other groups may still use the callbacks.
    return Syscall_InterruptBusClose(handle, principal, device);
}

oserr_t
DeviceInterruptReap(
    _In_ uuid_t handle)
{
    // The driver process must have exited before the kernel removes its
    // allocation. A missing reply alone does not allow cleanup.
    return Syscall_InterruptReap(handle);
}

oserr_t
DeviceInterruptMessageControllerResolve(
    _In_  const InterruptMessageControllerIdentity_t* query,
    _Out_ uuid_t*                                     idOut)
{
    // Look up a controller discovered by the kernel. This creates no domain.
    return Syscall_InterruptMessageControllerResolve(query, idOut);
}

oserr_t
DeviceInterruptLineSourceResolve(
    _In_  const InterruptLineSourceIdentity_t* query,
    _Out_ uuid_t*                              idOut)
{
    // Verify firmware against the active controller before exposing the source.
    return Syscall_InterruptLineSourceResolve(query, idOut);
}
