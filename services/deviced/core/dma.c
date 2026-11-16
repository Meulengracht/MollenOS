/**
 * MollenOS
 *
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
 */

#include <ddk/utils.h>
#include <internal/_syscalls.h>
#include <os/types/dma.h>
#include <os/types/device.h>
#include <os/usched/mutex.h>
#include <stdlib.h>
#include <string.h>

#include <core/dma.h>
#include "dma-private.h"
#include "device-private.h"

struct usched_mtx g_dmDmaLock;
DmDmaState_t      g_dmDmaState;

// Call with the lifecycle lock held. These lists are limited by the kernel's
// attachment limit. They are only used while publishing and controlling devices,
// never during driver DMA I/O.
static DmDmaAttachment_t*
__FindDmaDevice(
    _In_ uuid_t device)
{
    DmDmaAttachment_t* entry;

    for (entry = g_dmDmaState.Devices; entry != NULL; entry = entry->Next) {
        if (entry->Device == device) {
            return entry;
        }
    }
    return NULL;
}

static DmDmaHost_t*
__FindDmaHost(
    _In_ uuid_t device)
{
    DmDmaHost_t* host;

    for (host = g_dmDmaState.Hosts; host != NULL; host = host->Next) {
        if (host->Device == device) {
            return host;
        }
    }
    return NULL;
}

// Build the kernel's input from the saved route. Unused range entries and
// reserved fields are zeroed on purpose, because the kernel checks them when
// it validates the registration.
static oserr_t
__RegisterDmaAttachment(
    _InOut_ DmDmaAttachment_t* entry)
{
    OSDmaAttachmentParameters_t parameters = { 0 };
    const DmDmaDescription_t*   description = &entry->Description;
    oserr_t                     status;
    uint32_t                    i;

    if (description->Map.Count == 0 || description->Map.Count > DMA_MAX_RANGES ||
        description->HostId == UUID_INVALID ||
        (description->CachePolicy != DmDmaCacheCoherent &&
         description->CachePolicy != DmDmaCacheNonCoherent)) {
        ERROR("__RegisterDmaAttachment: device %u has an invalid route", entry->Device);
        return OS_EINVALPARAMS;
    }

    parameters.Version = DMA_ATTACHMENT_VERSION;
    parameters.Device = entry->Device;
    parameters.Host = description->HostId;
    parameters.RequesterGroup = description->HostId;
    parameters.ResetGroup = description->HostId;
    parameters.AddressLimit = description->AddressLimit;
    parameters.CachePolicy = description->CachePolicy;
    parameters.RangeCount = description->Map.Count;

    for (i = 0; i < parameters.RangeCount; i++) {
        parameters.Ranges[i].PhysicalBase = description->Map.Ranges[i].PhysicalBase;
        parameters.Ranges[i].DeviceBase = description->Map.Ranges[i].DeviceBase;
        parameters.Ranges[i].Length = description->Map.Ranges[i].Length;
    }

    status = DMAAttachmentRegister(
        g_dmDmaState.Manager,
        &parameters,
        &entry->Id
    );
    if (status != OS_EOK) {
        WARNING("__RegisterDmaAttachment: device %u registration failed: %u", entry->Device, status);
    }
    return status;
}

oserr_t
DmDmaInitialize(void)
{
    oserr_t status;
    oserr_t cleanup;

    // Startup runs on a single thread, before any device is discovered. Never
    // replace the manager after a failure, because the failed call may already
    // have created state in the kernel.
    if (g_dmDmaState.ManagerState != DmDmaManagerNotStarted) {
        return g_dmDmaState.ManagerState == DmDmaManagerReady ? OS_EOK : OS_ECANCELLED;
    }
    g_dmDmaState.ManagerState = DmDmaManagerUnavailable;

    status = DMAManagerCreate(&g_dmDmaState.Manager);
    if (status != OS_EOK) {
        ERROR("DmDmaInitialize: manager creation failed: %u", status);
        return status;
    }

    // The queued worker may start on another scheduler thread right away. Hold
    // the lifecycle mutex so it waits until queue setup has either finished or
    // failed, and so ManagerState always matches the manager it will see.
    usched_mtx_lock(&g_dmDmaLock);
    g_dmDmaState.ManagerState = DmDmaManagerReady;

    status = DmDmaRecoveryInitialize(g_dmDmaState.Manager);
    if (status != OS_EOK) {
        ERROR("DmDmaInitialize: event subscription failed: %u", status);
        g_dmDmaState.ManagerState = DmDmaManagerUnavailable;
        usched_mtx_unlock(&g_dmDmaLock);

        cleanup = Syscall_HandleDestroy(g_dmDmaState.Manager);
        if (cleanup != OS_EOK && cleanup != OS_EINCOMPLETE && cleanup != OS_ENOENT) {
            ERROR("DmDmaInitialize: manager rollback failed: %u", cleanup);
        } else {
            g_dmDmaState.Manager = UUID_INVALID;
        }
        return status;
    }
    usched_mtx_unlock(&g_dmDmaLock);
    return OS_EOK;
}

oserr_t
DmDmaRegisterDevice(
    _In_ uuid_t device)
{
    DmDmaAttachment_t* entry;
    DmDmaHost_t*       host;
    DmDmaDescription_t description;
    oserr_t            status;

    usched_mtx_lock(&g_dmDmaLock);
    if (g_dmDmaState.ManagerState != DmDmaManagerReady) {
        status = OS_ECANCELLED;
        goto done;
    }

    entry = __FindDmaDevice(device);
    if (entry != NULL) {
        if (entry->RouteState == DmDmaRouteUnavailable || entry->Id.Value != 0) {
            status = OS_EOK;
        } else {
            status = __RegisterDmaAttachment(entry);
        }
        goto done;
    }

    if (g_dmDmaState.DeviceCount == DMA_MANAGER_MAX_ATTACHMENTS) {
        status = OS_EOVERFLOW;
        goto done;
    }

    status = DmDeviceDescribeDma(device, &description);
    if (status != OS_EOK && status != OS_ENOTSUPPORTED) {
        WARNING("DmDmaRegisterDevice: device %u description failed: %u", device, status);
        goto done;
    }

    entry = calloc(1, sizeof(DmDmaAttachment_t));
    if (entry == NULL) {
        ERROR("DmDmaRegisterDevice: device %u record allocation failed", device);
        status = OS_EOOM;
        goto done;
    }

    entry->Device = device;
    entry->RouteState = status == OS_EOK ? DmDmaRouteReady : DmDmaRouteUnavailable;
    if (entry->RouteState == DmDmaRouteReady) {
        entry->Description = description;

        host = __FindDmaHost(description.HostId);
        if (host == NULL) {
            host = calloc(1, sizeof(DmDmaHost_t));
            if (host == NULL) {
                ERROR("DmDmaRegisterDevice: host %u record allocation failed", description.HostId);
                free(entry);
                status = OS_EOOM;
                goto done;
            }

            host->Device = description.HostId;
            host->Next = g_dmDmaState.Hosts;

            g_dmDmaState.Hosts = host;
        }
        host->Devices++;
        entry->Host = host;
    }

    // Save the route before calling the kernel. If registration fails, it is
    // retried using this copy. If the syscall itself fails, the first
    // registration is cancelled.
    entry->Next = g_dmDmaState.Devices;
    g_dmDmaState.Devices = entry;
    g_dmDmaState.DeviceCount++;

    if (entry->RouteState == DmDmaRouteReady) {
        status = __RegisterDmaAttachment(entry);
    } else {
        status = OS_EOK;
    }

done:
    usched_mtx_unlock(&g_dmDmaLock);
    return status;
}

oserr_t
DmDmaRemoveDevice(
    _In_ uuid_t device)
{
    DmDmaAttachment_t** link;
    DmDmaAttachment_t*  entry;
    DmDmaHost_t**       hostLink;
    DmDmaHost_t*        host;
    DmDevice_t*         registered;
    oserr_t             status = OS_EOK;

    usched_mtx_lock(&g_dmDmaLock);
    
    usched_mtx_lock(&g_dmDevicesLock);
    registered = DmDeviceFindLocked(device);
    if (registered == NULL || !registered->Removing) {
        usched_mtx_unlock(&g_dmDevicesLock);
        status = OS_EBUSY;
        goto done;
    }
    usched_mtx_unlock(&g_dmDevicesLock);
    
    for (link = &g_dmDmaState.Devices; *link != NULL; link = &(*link)->Next) {
        if ((*link)->Device == device) {
            break;
        }
    }
    entry = *link;
    if (entry == NULL) {
        goto done;
    }
    
    if (entry->Id.Value != 0) {
        status = DmDmaDrainEventsLocked();
        if (status != OS_EOK) {
            goto done;
        }
    }

    if (entry->Binding.Lease != NULL) {
        status = OS_EBUSY;
        goto done;
    }

    if (entry->Id.Value != 0) {
        if (g_dmDmaState.ManagerState != DmDmaManagerReady) {
            status = OS_ECANCELLED;
            goto done;
        }
        status = DMAAttachmentRemove(g_dmDmaState.Manager, &entry->Id);
        if (status != OS_EOK) {
            WARNING("DmDmaRemoveDevice: device %u attachment removal failed: %u", device, status);
            goto done;
        }
    }
    
    *link = entry->Next;
    host = entry->Host;
    if (host != NULL && --host->Devices == 0) {
        for (hostLink = &g_dmDmaState.Hosts; *hostLink != host; hostLink = &(*hostLink)->Next) {
        }
        *hostLink = host->Next;
        free(host);
    }
    g_dmDmaState.DeviceCount--;
    free(entry);

done:
    usched_mtx_unlock(&g_dmDmaLock);
    return status;
}

oserr_t
DmDmaCheckAssignmentLocked(
    _In_ uuid_t   device,
    _In_ uint64_t principal)
{
    DmDmaAttachment_t* entry = __FindDmaDevice(device);

    if (principal == 0 || (entry != NULL && entry->Binding.Principal != 0 &&
                          entry->Binding.Principal != principal)) {
        return OS_EPERMISSIONS;
    }
    return OS_EOK;
}

// Call with the lifecycle mutex held. Once the root device is being removed, no
// new bindings or shared startup are allowed, even if the child device has not
// been marked as removing yet.
static bool
__DmaHostCanStart(
    _In_ DmDmaHost_t* host)
{
    DmDevice_t* root;
    bool        ready;

    if (host->State != DmDmaHostReady) {
        return false;
    }

    usched_mtx_lock(&g_dmDevicesLock);
    root = DmDeviceFindLocked(host->Device);
    ready = root != NULL && !root->Removing;
    usched_mtx_unlock(&g_dmDevicesLock);
    return ready;
}

oserr_t
DmDmaCheckControlLocked(
    _In_ uuid_t   device,
    _In_ uint64_t principal,
    _In_ bool     enableDma)
{
    DmDevice_t*           registered;
    DmDmaAttachment_t*    entry;
    OSDmaBindParameters_t request;
    OSDmaBindingResult_t  result;
    oserr_t               status;

    usched_mtx_lock(&g_dmDevicesLock);
    registered = DmDeviceFindLocked(device);
    if (registered == NULL) {
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_ENOENT;
    }
    if (registered->Removing) {
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_EBUSY;
    }

    if (principal == 0 || registered->DriverId == UUID_INVALID || registered->DriverPrincipal != principal) {
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_EPERMISSIONS;
    }
    usched_mtx_unlock(&g_dmDevicesLock);
    
    if (!enableDma) {
        return OS_EOK;
    }

    entry = __FindDmaDevice(device);
    if (g_dmDmaState.ManagerState != DmDmaManagerReady || entry == NULL ||
        entry->RouteState == DmDmaRouteUnavailable || entry->Id.Value == 0) {
        return OS_ENOTSUPPORTED;
    }

    if (entry->RouteState == DmDmaRouteChanged || entry->Binding.Lease == NULL) {
        return OS_ECANCELLED;
    }

    if (!__DmaHostCanStart(entry->Host)) {
        return OS_ECANCELLED;
    }

    // A grant number we saved earlier does not prove the driver still has it.
    // Replay the request to ask the kernel, while holding the lock so recovery
    // cannot run at the same time.
    request = (OSDmaBindParameters_t) {
        .Attachment = entry->Id.Value,
        .Principal = principal,
        .RequestId = entry->Binding.RequestId
    };

    status = DMAAttachmentBind(
        g_dmDmaState.Manager,
        &request,
        &result
    );
    if (status != OS_EOK) {
        WARNING("DmDmaCheckControlLocked: device %u binding check failed: %u", device, status);
        return status;
    }

    if (result.Handle != entry->Binding.Result.Handle || result.Generation != entry->Binding.Result.Generation) {
        return OS_ECANCELLED;
    }
    return OS_EOK;
}

static bool
__SameDmaDescription(
    _In_ const DmDmaDescription_t* left,
    _In_ const DmDmaDescription_t* right)
{
    // Compare only the fields that mean something. Padding and unused range
    // slots hold no route information, so they must not make two otherwise
    // identical descriptions look different.
    if (left->DeviceId != right->DeviceId || left->HostId != right->HostId ||
        left->AddressLimit != right->AddressLimit || left->CachePolicy != right->CachePolicy ||
        left->Map.Count != right->Map.Count || left->Map.Count > DM_DMA_MAX_RANGES) {
        return false;
    }

    for (uint32_t i = 0; i < left->Map.Count; i++) {
        if (left->Map.Ranges[i].PhysicalBase != right->Map.Ranges[i].PhysicalBase ||
            left->Map.Ranges[i].DeviceBase != right->Map.Ranges[i].DeviceBase ||
            left->Map.Ranges[i].Length != right->Map.Ranges[i].Length) {
            return false;
        }
    }
    return true;
}

oserr_t
DmDmaBind(
    _In_  uint64_t              principal,
    _In_  uuid_t                device,
    _In_  uint64_t              requestId,
    _Out_ OSDmaBindingResult_t* bindingOut)
{
    DmDmaAttachment_t*    entry;
    struct DmDmaLease*    lease = NULL;
    OSDmaBindParameters_t request;
    OSDmaBindingResult_t  result;
    oserr_t               status;

    if (bindingOut == NULL || requestId == 0) {
        return OS_EINVALPARAMS;
    }

    if (requestId == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    usched_mtx_lock(&g_dmDmaLock);
    status = DmDmaCheckControlLocked(device, principal, false);
    if (status != OS_EOK) {
        WARNING("DmDmaBind: device %u caller %llu is not assigned: %u",
                device, (unsigned long long)principal, status);
        goto done;
    }

    status = DmDmaDrainEventsLocked();
    if (status != OS_EOK) {
        goto done;
    }

    entry = __FindDmaDevice(device);
    if (entry == NULL || entry->RouteState == DmDmaRouteUnavailable || entry->Id.Value == 0) {
        status = OS_ENOTSUPPORTED;
        goto done;
    }

    if (entry->RouteState == DmDmaRouteChanged || !__DmaHostCanStart(entry->Host)) {
        status = OS_ECANCELLED;
        goto done;
    }

    status = DmDmaCheckAssignmentLocked(device, principal);
    if (status != OS_EOK) {
        goto done;
    }

    if (requestId < entry->Binding.RequestId ||
        (requestId == entry->Binding.RequestId && entry->Binding.Lease == NULL)) {
        status = OS_ECANCELLED;
        goto done;
    }

    if (entry->Binding.Lease != NULL && requestId != entry->Binding.RequestId) {
        status = OS_EBUSY;
        goto done;
    }

    request = (OSDmaBindParameters_t) {
        .Attachment = entry->Id.Value,
        .Principal = principal,
        .RequestId = requestId
    };

    if (entry->Binding.Lease == NULL) {
        status = DmDevicePrepareDmaForPrincipal(
            device,
            principal,
            &lease
        );
        if (status != OS_EOK) {
            WARNING("DmDmaBind: device %u active preparation failed: %u", device, status);
            goto done;
        }

        if (!__SameDmaDescription(&entry->Description, DmDmaLeaseGetDescription(lease))) {
            ERROR("DmDmaBind: device %u route changed after registration", device);
            entry->RouteState = DmDmaRouteChanged;
            DmDmaLeaseRelease(&lease);
            status = OS_ECANCELLED;
            goto done;
        }
    }

    status = DMAAttachmentBind(
        g_dmDmaState.Manager,
        &request,
        &result
    );
    if (status != OS_EOK) {
        WARNING("DmDmaBind: device %u kernel binding failed: %u", device, status);
        DmDmaLeaseRelease(&lease);
        goto done;
    }

    if (lease != NULL) {
        // Save the binding before replying. If the reply fails to send, we
        // cannot know whether the driver got its grant, so the lease is kept
        // until the final idle event arrives.
        entry->Binding.Principal = principal;
        entry->Binding.RequestId = requestId;
        entry->Binding.Lease = lease;
        entry->Binding.Result = result;
        entry->Binding.Idle = false;
    }

    *bindingOut = result;

done:
    usched_mtx_unlock(&g_dmDmaLock);
    return status;
}

oserr_t
DmDmaBeginHostStart(
    _In_ uuid_t host)
{
    DmDmaHost_t* entry;
    oserr_t      status;

    usched_mtx_lock(&g_dmDmaLock);
    status = DmDmaDrainEventsLocked();
    if (status != OS_EOK) {
        usched_mtx_unlock(&g_dmDmaLock);
        return status;
    }

    entry = __FindDmaHost(host);
    if (g_dmDmaState.ManagerState != DmDmaManagerReady || entry == NULL || !__DmaHostCanStart(entry)) {
        usched_mtx_unlock(&g_dmDmaLock);
        return OS_ECANCELLED;
    }

    // On success the lock is deliberately left held and passes to the caller
    // that starts the hardware. That caller must call DmDmaEndHostStart after
    // it has released its bus locks.
    return OS_EOK;
}

void
DmDmaEndHostStart(void)
{
    usched_mtx_unlock(&g_dmDmaLock);
}
