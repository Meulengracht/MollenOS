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
 */

#include <internal/_syscalls.h>
#include <os/handle.h>

oserr_t
OSHandleRegisterProvider(
    _In_  OSHandle_t* notificationQueue,
    _Out_ OSHandle_t* registrationOut)
{
    uuid_t  id;
    oserr_t status;

    if (notificationQueue == NULL || registrationOut == NULL) {
        return OS_EINVALPARAMS;
    }
    
    status = Syscall_HandleProviderRegister(notificationQueue->ID, &id);
    if (status != OS_EOK) {
        return status;
    }
    return OSHandleWrap(id, OSHANDLE_NULL, NULL, true, registrationOut);
}

oserr_t
OSHandleImport(
    _In_  const GrantToken_t* token,
    _Out_ OSHandle_t*         handleOut)
{
    uuid_t  id;
    oserr_t status;

    if (handleOut == NULL) {
        return OS_EINVALPARAMS;
    }

    status = Syscall_HandleImport(token, &id);
    if (status != OS_EOK) {
        return status;
    }
    return OSHandleWrap(id, OSHANDLE_NULL, NULL, true, handleOut);
}

oserr_t
OSHandleValidate(
    _In_    OSHandle_t* registration,
    _In_    uint64_t    callerGroupID,
    _In_    uuid_t      referenceID,
    _In_    uint32_t    requiredAccess,
    _Out_   uuid_t*     resourceIDOut)
{
    if (registration == NULL) {
        return OS_EINVALPARAMS;
    }

    return Syscall_HandleValidate(
        registration->ID,
        &callerGroupID,
        referenceID,
        requiredAccess,
        resourceIDOut
    );
}

oserr_t
OSHandleReadCleanup(
    _In_  OSHandle_t* registration,
    _Out_ uuid_t*     resourceIDs,
    _In_  uint32_t    capacity,
    _Out_ uint32_t*   countOut)
{
    if (registration == NULL) {
        return OS_EINVALPARAMS;
    }

    return Syscall_HandleProviderGetEvents(
        registration->ID,
        resourceIDs,
        capacity,
        countOut
    );
}
