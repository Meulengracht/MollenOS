/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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

#define __MODULE "SCIF"
//#define __TRACE

#include <assert.h>
#include <debug.h>
#include <capabilities.h>
#include <handles/global.h>
#include <handles/grants.h>
#include <handles/handle.h>
#include <handles/provider.h>
#include <handles/paths.h>
#include <handles/set.h>
#include <ioset.h>
#include <os/types/syscall.h>
#include <memoryspace.h>
#include <shm/view.h>

oserr_t
ScCreateHandle(
    _In_  uuid_t   registrationID,
    _In_  uint32_t accessMask,
    _Out_ uuid_t*  handleOut)
{
    uuid_t handle;
    oserr_t status;

    status = HandleProviderCreate(registrationID, accessMask, &handle);
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceCopyToUser(handleOut, &handle, sizeof(handle));
    if (status != OS_EOK) {
        // The creator never got the handle, so the service has nothing to clean
        // up for it. Cancel the cleanup notification before we drop the grant.
        HandleProviderCancelCreate(handle);
        DestroyHandle(handle);
    }
    return status;
}

oserr_t
ScHandleRegisterProvider(
    _In_  uuid_t  notificationQueueID,
    _Out_ uuid_t* registrationOut)
{
    uuid_t  registration;
    oserr_t cleanupStatus;
    oserr_t status;

    status = HandleProviderRegister(notificationQueueID, &registration);
    if (status != OS_EOK) {
        return status;
    }

    status = MemorySpaceCopyToUser(registrationOut, &registration, sizeof(registration));
    if (status != OS_EOK) {
        cleanupStatus = ControlHandleSet(
            notificationQueueID,
            IOSET_DEL,
            registration,
            NULL
        );
        if (cleanupStatus != OS_EOK && cleanupStatus != OS_ENOENT) {
            ERROR("Failed to remove unpublished provider registration event: %d", cleanupStatus);
        }
        DestroyHandle(registration);
    }
    return status;
}

oserr_t
ScHandleImport(
    _In_  const GrantToken_t* userToken,
    _In_  uuid_t              destinationMemorySpace,
    _Out_ uuid_t*             referenceOut)
{
    GrantToken_t         token;
    SHMHandle_t          view;
    MemorySpace_t*       memorySpace;
    MSContextSecurity_t* receiver = NULL;
    uuid_t               reference;
    oserr_t              status;

    // The new handle can also be created in another process, which processd uses
    // to pass handles on to a child process when starting it. Knowing the ID of
    // a memory space is not enough for this. The caller must also have the
    // capability to manage memory spaces, and a grant for the memory space. Only
    // application memory spaces can be used, as kernel memory spaces have no
    // group to put the handle in.
    // We keep a reference on the group of the memory space until we are done.
    // That way it stays alive while we copy the new ID to userspace (and remove
    // the handle again if that fails), even if the memory space is closed in the
    // meantime.
    if (destinationMemorySpace != UUID_INVALID) {
        status = CapabilityVerify(CAPABILITY_MEMORYSPACE_MANAGEMENT);
        if (status != OS_EOK) {
            return status;
        }

        status = AcquireHandleReference(
            destinationMemorySpace,
            HandleTypeMemorySpace,
            (void**)&memorySpace
        );
        if (status != OS_EOK) {
            return status;
        }

        if (!(memorySpace->Flags & MEMORY_SPACE_APPLICATION)) {
            ReleaseGlobalHandle(destinationMemorySpace);
            return OS_EINVALPARAMS;
        }

        status = MSContextAcquireSecurity(memorySpace->GroupGlobalHandleID, &receiver);
        ReleaseGlobalHandle(destinationMemorySpace);
        if (status != OS_EOK) {
            return status;
        }
    }

    status = MemorySpaceCopyFromUser(&token, userToken, sizeof(token));
    if (status != OS_EOK) {
        goto exit;
    }

    // Both kinds of tokens are handled the same way. The new handle is created in
    // the receiver (or in the caller, when no memory space was given), and it is
    // removed again below if we fail to copy the ID to userspace.
    // For SHM the new handle is a view, which only gives access to the part of
    // the buffer the token allows, and which is not mapped into memory yet. For
    // services it is a new reference to the resource, with the access the token
    // allows.
    switch (token.Options.ResourceType) {
        case GrantTokenResourceTypeProvider:
            status = HandleProviderImport(&token, receiver, &reference);
            break;
        case GrantTokenResourceTypeSHM:
            status = SHMAttachToken(&token, receiver, &view);
            if (status == OS_EOK) {
                reference = view.ID;
            }
            break;
        default:
            status = OS_ENOTSUPPORTED;
            break;
    }
    if (status != OS_EOK) {
        goto exit;
    }

    status = MemorySpaceCopyToUser(referenceOut, &reference, sizeof(reference));
    if (status != OS_EOK) {
        // We can't undo the import itself, which means a single-use token stays
        // used up. So we only remove the new handle again, and we remove it from
        // the receiver, as that is where it was created.
        DestroyHandleInContext(
            receiver != NULL ? receiver : MemorySpaceGetSecurityContext(), reference
        );
    }

exit:
    // MSContextAcquireSecurity took a reference on the group, which we release
    // again here.
    if (receiver != NULL) {
        ReleaseGlobalHandle(receiver->GlobalHandleID);
    }
    return status;
}

oserr_t
ScHandleValidate(
    _In_  uuid_t          registrationID,
    _In_  const uint64_t* userGroupID,
    _In_  uuid_t          referenceID,
    _In_  uint32_t        accessMask,
    _Out_ uuid_t*         resourceOut)
{
    uint64_t groupID;
    uuid_t   resource;
    oserr_t  status;

    // The 64-bit group ID is passed by pointer, so the syscall ABI is the same
    // on i386 and 64-bit targets. We copy it once, before any grant table is read.
    status = MemorySpaceCopyFromUser(&groupID, userGroupID, sizeof(uint64_t));
    if (status != OS_EOK) {
        return status;
    }

    status = HandleProviderValidate(
        registrationID,
        groupID,
        referenceID,
        accessMask,
        &resource
    );
    if (status == OS_EOK) {
        status = MemorySpaceCopyToUser(resourceOut, &resource, sizeof(uuid_t));
    }
    return status;
}

oserr_t
ScHandleProviderGetEvents(
    _In_  uuid_t    registrationID,
    _Out_ uuid_t*   resourceIDs,
    _In_  uint32_t  capacity,
    _Out_ uint32_t* countOut)
{
    return HandleProviderGetEvents(
        registrationID,
        resourceIDs,
        capacity,
        countOut
    );
}

oserr_t
ScSetHandleActivity(
        _In_ uuid_t  Handle,
        _In_ unsigned int Flags)
{
    return MarkHandle(Handle, Flags);
}

oserr_t
ScLookupHandle(
        _In_  const char* Path,
        _Out_ uuid_t*     HandleOut)
{
    return LookupHandleByPath(Path, HandleOut);
}

oserr_t
ScDestroyHandle(
        _In_ uuid_t Handle)
{
    if (Handle == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    
    // CPU and DMA resources use the release syscall, which checks their operation
    // state. Generic close must not consume a suspended begin's reference.
    if (LookupHandle(Handle, HandleTypeSHMAccess) != NULL) {
        return OS_EINVALPARAMS;
    }
    return DestroyHandle(Handle);
}

oserr_t
ScGrantTokenCreate(
    _In_  uuid_t                     handleID,
    _In_  const GrantTokenOptions_t* userOptions,
    _Out_ GrantToken_t*              tokenOut)
{
    GrantTokenOptions_t options;
    GrantToken_t        token;
    oserr_t             oserr;
    oserr_t             cleanupStatus;

    // Copy the options once so the caller cannot change them during validation
    // or export creation.
    oserr = MemorySpaceCopyFromUser(&options, userOptions, sizeof(GrantTokenOptions_t));
    if (oserr != OS_EOK) {
        ERROR("ScGrantTokenCreate: options copy for handle %u failed: %u", handleID, oserr);
        return oserr;
    }

    oserr = GrantTokenCreate(handleID, &options, &token);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MemorySpaceCopyToUser(tokenOut, &token, sizeof(GrantToken_t));
    if (oserr != OS_EOK) {
        ERROR("ScGrantTokenCreate: token copy for control %u failed: %u",
              token.ControlHandleID, oserr);
        goto error;
    }
    return OS_EOK;

error:
    cleanupStatus = GrantTokenRevoke(token.ControlHandleID);
    // If another thread already closed the control handle, the export is
    // disabled and no further cleanup is needed.
    if (cleanupStatus != OS_EOK && cleanupStatus != OS_ENOENT) {
        ERROR("ScGrantTokenCreate: cancel control %u failed: %u (original status %u)",
              token.ControlHandleID, cleanupStatus, oserr);
    }
    return oserr;
}

oserr_t
ScGrantTokenRevoke(
    _In_ uuid_t controlHandleID)
{
    return GrantTokenRevoke(controlHandleID);
}

oserr_t
ScCreateHandleSet(
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut)
{
    if (!handleOut) {
        return OS_EINVALPARAMS;
    }
    
    *handleOut = CreateHandleSet(flags);
    if (*handleOut != UUID_INVALID) {
        return OS_EOK;
    }
    return OS_EOOM;
}

oserr_t
ScControlHandleSet(
        _In_ uuid_t              setHandle,
        _In_ int                 operation,
        _In_ uuid_t              handle,
        _In_ struct ioset_event* event)
{
    return ControlHandleSet(setHandle, operation, handle, event);
}

oserr_t
ScListenHandleSet(
        _In_  uuid_t                     handle,
        _In_  OSAsyncContext_t*          asyncContext,
        _In_  HandleSetWaitParameters_t* parameters,
        _Out_ int*                       numberOfEventsOut)
{
    if (!parameters || !numberOfEventsOut) {
        return OS_EINVALPARAMS;
    }
    return WaitForHandleSet(
            handle,
            asyncContext,
            parameters->Events,
            parameters->MaxEvents,
            parameters->PollEvents,
            parameters->Deadline,
            numberOfEventsOut
    );
}
