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

#include <ddk/interrupt-provider.h>
#include <internal/_syscalls.h>

oserr_t
DeviceInterruptCompletionCreate(
    _Out_ uuid_t* handleOut)
{
    // Keep creation separate from binding so a mapping failure still leaves the
    // provider with a handle it can release. The kernel writes the result directly.
    return Syscall_InterruptCompletionCreate(handleOut);
}

oserr_t
DeviceInterruptCompletionBind(
    _In_ uuid_t                          handle,
    _In_ const InterruptResourceTable_t* resources)
{
    // The kernel copies and validates this description before mapping resources.
    // Return its error unchanged so the provider knows whether cleanup is needed.
    return Syscall_InterruptCompletionBind(handle, resources);
}

oserr_t
DeviceInterruptCompletionRelease(
    _In_ uuid_t handle)
{
    // Only the kernel knows whether any group still uses this callback. Do not
    // hide a busy result or a cleanup failure: the provider must retain the handle.
    return Syscall_InterruptCompletionRelease(handle);
}

oserr_t
DeviceInterruptProviderAttach(
    _In_ const struct InterruptProviderAttachRequest* request)
{
    // Forward the whole group in one call so attachment cannot stop halfway
    // through the vector list. A failed result copy may still leave it attached;
    // the API lets the provider retry the same request to retrieve its messages.
    return Syscall_InterruptProviderAttach(request);
}

oserr_t
DeviceInterruptProviderCheckEnabled(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    // Ask the kernel instead of caching the state here: the device driver can
    // enable or disable its group independently of the provider.
    return Syscall_InterruptProviderCheckEnabled(handle, principal, device);
}

oserr_t
DeviceInterruptProviderClose(
    _In_ uuid_t handle,
    _In_ uuid_t principal,
    _In_ uuid_t device)
{
    // Closing drops this group's uses of the callbacks. Their handles remain
    // owned by the provider, which may have attached them to other groups too.
    return Syscall_InterruptProviderClose(handle, principal, device);
}

oserr_t
DeviceInterruptReap(
    _In_ uuid_t handle)
{
    // The kernel checks that the owning process has died before removing its
    // allocation. A missing reply from the driver alone is not enough evidence.
    return Syscall_InterruptReap(handle);
}
