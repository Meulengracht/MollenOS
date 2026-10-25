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

//#define __TRACE

#include <ddk/types/interrupt-group.h>
#include <ddk/busdevice.h>
#include <ddk/interrupt.h>
#include <ddk/pci-interrupt.h>
#include "interrupt-private.h"
#include <ddk/types/interrupt-source.h>
#include <ddk/utils.h>
#include <os/device.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static oserr_t
__ProviderControl(
    _In_ DeviceInterrupt_t*                interrupt,
    _In_ enum __InterruptProviderOperation operation)
{
    struct DeviceInterruptState*        state = interrupt->State;
    struct DeviceInterruptSourceRequest source = { .Allocation = state->Handle };
    oserr_t                             status;

    // Translate neutral lifecycle commands at the protocol boundary. Native
    // sources and PCI capabilities share ordering, not a wire format or enum.
    if (state->PlatformSource) {
        switch (operation) {
            case InterruptProviderReserve:
                source.Operation = DeviceInterruptSourceReserve;
                source.Index = interrupt->SourceIndex;
                break;
            case InterruptProviderEnable:
                source.Operation = DeviceInterruptSourceEnable;
                break;
            case InterruptProviderDisable:
                source.Operation = DeviceInterruptSourceDisable;
                break;
            case InterruptProviderRelease:
                source.Operation = DeviceInterruptSourceRelease;
                break;
            default:
                return OS_EINVALPARAMS;
        }

        status = OSDeviceIOCtl(
            interrupt->DeviceId,
            OSIOCTLREQUEST_INTERRUPT_CONTROL,
            &source,
            sizeof(source)
        );
    } else {
        status = __InterruptPciControl(interrupt, operation);
    }

    TRACE("__ProviderControl(device=%u, allocation=%u, operation=%u, platform=%d): %u",
          interrupt->DeviceId, state->Handle, operation, state->PlatformSource, status);
    if (status != OS_EOK && status != OS_EBUSY) {
        ERROR("__ProviderControl: device %u allocation %u operation %u failed: %u",
              interrupt->DeviceId, state->Handle, operation, status);
    }
    return status;
}

static oserr_t
__ReserveProvider(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    struct DeviceInterruptState* state = interrupt->State;
    oserr_t                      status;

    // Record ownership before RPC: a lost reply can conceal a reservation.
    // EBUSY alone promises that the provider was left untouched.
    state->ProviderReserved = 1;
    
    status = __ProviderControl(interrupt, InterruptProviderReserve);
    if (status != OS_EOK) {
        if (status == OS_EBUSY) {
            state->ProviderReserved = 0;
        }
        return status;
    }
    
    if (state->PlatformSource) {
        // Platform reserve includes translation/programming under the provider's
        // lease. The common lifecycle does not program its upstream hardware.
        return OS_EOK;
    }
    return __InterruptPciProgram(interrupt);
}

void
DeviceInterruptInitialize(
    _Out_ DeviceInterrupt_t* interrupt,
    _In_  Device_t*          device)
{
    const BusDevice_t* bus;

    // Initialization allocates nothing, so even a driver without a bus device
    // can prepare a line descriptor before requesting its reservation.
    memset(interrupt, 0, sizeof(DeviceInterrupt_t));
    interrupt->DeviceId = UUID_INVALID;
    interrupt->Line.Line = INTERRUPT_NONE;
    interrupt->Line.Pin = INTERRUPT_NONE;
    
    if (device != NULL) {
        interrupt->DeviceId = device->Id;
        if (device->Length == sizeof(BusDevice_t)) {
            bus = (const BusDevice_t*)device;
            interrupt->IsPci = bus->Type == BusTypePci;
            interrupt->Line.Line = bus->InterruptLine;
            interrupt->Line.Pin = bus->InterruptPin;
            interrupt->Line.AcpiConform = bus->InterruptAcpiConform;
        }
    }
}

oserr_t
DeviceInterruptSelectSource(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_ uint32_t index)
{
    if (interrupt == NULL || interrupt->IsPci) {
        return OS_EINVALPARAMS;
    }
    
    if (interrupt->State != NULL) {
        return OS_EBUSY;
    }

    // The provider validates the firmware index at allocation, after any
    // required device preparation. Selection itself takes no hardware lease.
    interrupt->SourceIndex = index;
    TRACE("DeviceInterruptSelectSource(device=%u, source=%u)", interrupt->DeviceId, index);
    return OS_EOK;
}

static oserr_t
__AllocateVectors(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    interrupt->Vectors = calloc(interrupt->Count, sizeof(DeviceInterruptVector_t));
    if (interrupt->Vectors == NULL) {
        ERROR("__AllocateVectors: device %u failed to allocate %u vectors", interrupt->DeviceId, interrupt->Count);
        return OS_EOOM;
    }

    for (uint32_t i = 0; i < interrupt->Count; i++) {
        interrupt->Vectors[i].AffinityMask = UINT32_MAX;
        interrupt->Vectors[i].ResourceTable.HandleResource = UUID_INVALID;
    }
    return OS_EOK;
}

static oserr_t
__ReserveSourceGroup(
    _In_  DeviceInterrupt_t*      interrupt,
    _In_  uint32_t                minimum,
    _Out_ InterruptGroupResult_t* result,
    _Out_ int*                    platformSource)
{
    struct DeviceInterruptSourceQuery source = {
        .Index = interrupt->SourceIndex
    };
    
    InterruptGroupRequest_t request = {
        .Kind = InterruptSourceLine,
        .Line = interrupt->Line,
        .Flags = interrupt->Flags,
        .Minimum = 1,
        .Optimal = 1
    };
    oserr_t status;

    if (minimum != 1) {
        return OS_ENOTSUPPORTED;
    }

    if (interrupt->DeviceId != UUID_INVALID && interrupt->Line.Line == INTERRUPT_NONE) {
        // Select by firmware source, independently of whether its provider
        // delivers a fixed line or translates that source into a message.
        status = OSDeviceIOCtl(
            interrupt->DeviceId,
            OSIOCTLREQUEST_INTERRUPT_QUERY,
            &source,
            sizeof(source)
        );
        if (status != OS_EOK) {
            WARNING("__ReserveSourceGroup: device %u source %u query failed: %u",
                    interrupt->DeviceId, interrupt->SourceIndex, status);
            return status;
        }

        if (source.Index != interrupt->SourceIndex) {
            return OS_EPROTOCOL;
        }

        if (source.Kind != InterruptSourceFixedLine && source.Kind != InterruptSourceMessage) {
            return OS_EPROTOCOL;
        }

        request.Kind = source.Kind;
        request.ControllerID = source.ControllerID;
        request.Line = source.Line;
        
        *platformSource = 1;
    } else if (interrupt->SourceIndex != 0) {
        return OS_EINVALPARAMS;
    }
    return Syscall_InterruptAllocate(&request, result);
}

oserr_t
__InterruptAllocate(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _In_    uint32_t           pciModes,
    _Out_   uint32_t*          countOut)
{
    InterruptGroupResult_t       result;
    struct DeviceInterruptState* state;
    oserr_t                      status;
    oserr_t                      cleanupStatus;

    if (countOut == NULL) {
        return OS_EINVALPARAMS;
    }

    *countOut = 0;
    
    if (interrupt == NULL || min == 0 || min > optimal) {
        return OS_EINVALPARAMS;
    }
    
    if (interrupt->Flags & ~INTERRUPT_EXCLUSIVE) {
        return OS_EINVALPARAMS;
    }
    
    if (interrupt->State != NULL) {
        return OS_EBUSY;
    }
    TRACE("__InterruptAllocate(device=%u, source=%u, count=%u/%u, pci=%u)",
          interrupt->DeviceId, interrupt->SourceIndex, min, optimal, interrupt->IsPci);

    // Allocate ownership storage first: rollback may itself fail after the
    // kernel or provider has reserved resources, including lost RPC replies.
    state = calloc(1, sizeof(struct DeviceInterruptState));
    if (state == NULL) {
        return OS_EOOM;
    }

    if (interrupt->IsPci) {
        status = __InterruptPciReserve(interrupt, min, optimal, pciModes, &result, &state->PciMode);
    } else {
        status = __ReserveSourceGroup(interrupt, min, &result, &state->PlatformSource);
    }
    if (status != OS_EOK) {
        TRACE("__InterruptAllocate: device %u reservation failed: %u", interrupt->DeviceId, status);
        free(state);
        return status;
    }
    
    state->Handle = result.Handle;
    interrupt->State = state;
    interrupt->Count = result.Count;
    interrupt->Kind = result.Kind;

    status = __AllocateVectors(interrupt);
    if (status != OS_EOK) {
        goto rollback;
    }
    
    if (state->PlatformSource || result.Kind == InterruptSourceMessage) {
        status = __ReserveProvider(interrupt);
        if (status != OS_EOK) {
            goto rollback;
        }
    }
    
    state->Phase = InterruptPhaseReady;
    
    *countOut = result.Count;
    return OS_EOK;

rollback:
    cleanupStatus = DeviceInterruptDestroy(interrupt);
    if (cleanupStatus != OS_EOK) {
        ERROR("__InterruptAllocate: device %u rollback failed: %u (setup %u); ownership retained",
              interrupt->DeviceId, cleanupStatus, status);
    }
    return status;
}

oserr_t
DeviceInterruptAllocate(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _Out_   uint32_t*          countOut)
{
    // Generic drivers accept provider-selected delivery. PCI drivers that
    // require a particular capability use the explicit PCI policy entry point.
    return __InterruptAllocate(
        interrupt,
        min,
        optimal,
        INTERRUPT_STRATEGY_ANY,
        countOut
    );
}

oserr_t
DeviceInterruptGetMessage(
    _In_  DeviceInterrupt_t*        interrupt,
    _In_  uint32_t                  index,
    _Out_ DeviceInterruptMessage_t* messageOut)
{
    DeviceInterruptMessage_t message;
    oserr_t                  status;

    if (interrupt == NULL || interrupt->State == NULL || messageOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (index >= interrupt->Count) {
        return OS_EINVALPARAMS;
    }

    if (interrupt->State->Phase != InterruptPhaseReady) {
        return OS_EBUSY;
    }

    status = Syscall_InterruptGetMessage(interrupt->State->Handle, index, &message);
    if (status == OS_EOK) {
        *messageOut = message;
    }
    return status;
}

// Resource edits are safe only while no kernel mapping or handler can use them.
static oserr_t
__GetEditableVector(
    _In_  DeviceInterrupt_t*        interrupt,
    _In_  uint32_t                  index,
    _Out_ DeviceInterruptVector_t** vectorOut)
{
    if (interrupt == NULL || interrupt->State == NULL || index >= interrupt->Count) {
        return OS_EINVALPARAMS;
    }

    // A pending provider stop can exist even with no kernel mappings. Do not
    // let resource edits bypass that obligation before a later enable retries it.
    assert(!interrupt->State->Enabled || interrupt->State->Bound);
    if (interrupt->State->Phase != InterruptPhaseReady ||
        interrupt->State->Bound || interrupt->State->PendingCleanup) {
        return OS_EBUSY;
    }
    
    *vectorOut = &interrupt->Vectors[index];
    return OS_EOK;
}

oserr_t
DeviceInterruptSetAffinity(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    uint32_t           affinityMask)
{
    DeviceInterruptVector_t* vector;
    oserr_t                  status;

    // INTx retains the platform's routing. Do not record a restriction that
    // the controller has not actually applied.
    status = __GetEditableVector(interrupt, index, &vector);
    if (status != OS_EOK) {
        return status;
    }

    if (affinityMask == 0) {
        return OS_EINVALPARAMS;
    }

    if (affinityMask != UINT32_MAX) {
        return OS_ENOTSUPPORTED;
    }
    
    vector->AffinityMask = affinityMask;
    return OS_EOK;
}

oserr_t
DeviceInterruptSetHandler(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    InterruptHandler_t handler)
{
    DeviceInterruptVector_t* vector;
    oserr_t                  status;

    // Defer mapping until Enable so the driver can finish describing resources.
    status = __GetEditableVector(interrupt, index, &vector);
    if (status != OS_EOK) {
        return status;
    }

    if (handler == NULL) {
        return OS_EINVALPARAMS;
    }
    
    vector->ResourceTable.Handler = handler;
    return OS_EOK;
}

oserr_t
DeviceInterruptSetDescriptor(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    int                descriptor)
{
    DeviceInterruptVector_t* vector;
    uuid_t                   handle;
    oserr_t                  status;

    // The kernel signals a native handle, not the driver's file descriptor.
    status = __GetEditableVector(interrupt, index, &vector);
    if (status != OS_EOK) {
        return status;
    }

    if (descriptor < 0) {
        return OS_EINVALPARAMS;
    }
    
    handle = GetNativeHandle(descriptor);
    if (handle == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    
    vector->ResourceTable.HandleResource = handle;
    return OS_EOK;
}

oserr_t
DeviceInterruptAddIoResource(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    DeviceIo_t*        ioSpace)
{
    DeviceInterruptVector_t* vector;
    oserr_t                  status;

    // Keep stable resource positions because fast handlers address them by index.
    status = __GetEditableVector(interrupt, index, &vector);
    if (status != OS_EOK) {
        return status;
    }

    if (ioSpace == NULL) {
        return OS_EINVALPARAMS;
    }
    
    for (unsigned int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (vector->ResourceTable.IoResources[i] == NULL) {
            vector->ResourceTable.IoResources[i] = ioSpace;
            return OS_EOK;
        }
    }
    return OS_EOOM;
}

oserr_t
DeviceInterruptAddMemoryResource(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    uintptr_t          address,
    _In_    size_t             length,
    _In_    unsigned int       flags)
{
    DeviceInterruptVector_t*       vector;
    FastInterruptMemoryResource_t* resource;
    oserr_t                        status;

    // Reject empty or wrapped ranges before they reach the kernel's mapper.
    status = __GetEditableVector(interrupt, index, &vector);
    if (status != OS_EOK) {
        return status;
    }

    if (address == 0 || length == 0) {
        return OS_EINVALPARAMS;
    }

    if (length > UINTPTR_MAX - address || (flags & ~INTERRUPT_RESOURCE_DISABLE_CACHE)) {
        return OS_EINVALPARAMS;
    }
    
    for (unsigned int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        resource = &vector->ResourceTable.MemoryResources[i];
        if (resource->Address == 0) {
            resource->Address = address;
            resource->Length = length;
            resource->Flags = flags;
            return OS_EOK;
        }
    }
    return OS_EOOM;
}

static oserr_t
__StopKernel(
    _InOut_ struct DeviceInterruptState* state)
{
    oserr_t status;

    // Caller has either stopped the provider or has not enabled it yet. This
    // is the only transition that discards kernel cleanup obligations, and only
    // after the kernel confirms synchronization and complete unmapping.
    assert(!state->Enabled || state->Bound);
    
    state->PendingCleanup = 1;
    if (state->Bound) {
        status = Syscall_InterruptDisable(state->Handle);
        if (status != OS_EOK) {
            ERROR("__StopKernel: allocation %u stop/unmap failed: %u; cleanup retained",
                  state->Handle, status);
            return status;
        }
    }

    state->Bound = 0;
    state->Enabled = 0;
    state->PendingCleanup = 0;
    return OS_EOK;
}

oserr_t
DeviceInterruptEnable(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    InterruptBindRequest_t       request;
    struct DeviceInterruptState* state;
    oserr_t                      status;
    oserr_t                      cleanup;

    TRACE("DeviceInterruptEnable(interrupt=%p)", interrupt);
    if (interrupt == NULL || interrupt->State == NULL) {
        WARNING("DeviceInterruptEnable: missing interrupt or allocation");
        return OS_EINVALPARAMS;
    }

    state = interrupt->State;
    if (state->Phase != InterruptPhaseReady) {
        TRACE("DeviceInterruptEnable: allocation %u phase %u is not ready",
              state->Handle, state->Phase);
        return OS_EBUSY;
    }

    // Failed bind, failed controller stop, and uncertain provider replies all
    // converge on this recovery path. Never trust cached Enabled before it.
    assert(!state->Enabled || state->Bound);
    assert(!state->Bound || state->Enabled || state->PendingCleanup);

    if (state->PendingCleanup) {
        status = DeviceInterruptDisable(interrupt);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    if (state->Enabled) {
        return OS_EOK;
    }

    for (uint32_t i = 0; i < interrupt->Count; i++) {
        if (interrupt->Vectors[i].ResourceTable.Handler == NULL) {
            WARNING("DeviceInterruptEnable: allocation %u index %u has no handler", state->Handle, i);
            return OS_EINVALPARAMS;
        }
    }

    // Register the cleanup duty before the first bind: even a failed bind may
    // own partial mappings. At every return, Bound without Enabled therefore
    // has PendingCleanup set until the shared stop helper confirms cleanup.
    state->PendingCleanup = 1;
    for (uint32_t i = 0; i < interrupt->Count; i++) {
        request.Index = i;
        request.Resources = interrupt->Vectors[i].ResourceTable;
        
        state->Bound = 1;
        
        status = Syscall_InterruptBind(state->Handle, &request);
        if (status != OS_EOK) {
            ERROR("DeviceInterruptEnable: allocation %u bind index %u failed: %u",
                  state->Handle, i, status);
            (void)__StopKernel(state);
            return status;
        }
    }

    status = Syscall_InterruptEnable(state->Handle);
    if (status != OS_EOK) {
        ERROR("DeviceInterruptEnable: allocation %u kernel enable failed: %u", state->Handle, status);
        (void)__StopKernel(state);
        return status;
    }

    state->Enabled = 1;
    
    if (state->ProviderReserved) {
        status = __ProviderControl(interrupt, InterruptProviderEnable);
        if (status != OS_EOK) {
            // The provider may deliver despite a lost reply. Keep the handlers
            // mapped until Disable confirms that upstream delivery is stopped.
            cleanup = DeviceInterruptDisable(interrupt);
            if (cleanup != OS_EOK) {
                ERROR("DeviceInterruptEnable: allocation %u rollback failed: %u",
                      state->Handle, cleanup);
            }
            return status;
        }
    }

    // Both kernel and provider delivery are confirmed. No incomplete transition
    // remains, so a repeated Enable can safely use the cached enabled state.
    state->PendingCleanup = 0;
    return OS_EOK;
}

oserr_t
DeviceInterruptDisable(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    struct DeviceInterruptState* state;
    oserr_t                      status;

    TRACE("DeviceInterruptDisable(interrupt=%p)", interrupt);
    if (interrupt == NULL) {
        WARNING("DeviceInterruptDisable: missing interrupt");
        return OS_EINVALPARAMS;
    }

    state = interrupt->State;
    if (state == NULL) {
        return OS_EOK;
    }

    // This marker belongs to every source, including manual/PCI INTx lines.
    // A failed stop can invalidate cached delivery state before returning.
    state->PendingCleanup = 1;
    if (state->ProviderReserved) {
        status = __ProviderControl(interrupt, InterruptProviderDisable);
        if (status != OS_EOK) {
            return status;
        }
    }
    return __StopKernel(state);
}

oserr_t
DeviceInterruptDestroy(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    oserr_t status;

    TRACE("DeviceInterruptDestroy(interrupt=%p)", interrupt);
    if (interrupt != NULL && interrupt->State != NULL) {
        // Destroy is terminal for every source because either kernel or
        // provider ownership may already be partly released on failure.
        // Ordinary Disable leaves the phase unchanged and remains reusable.
        interrupt->State->Phase = InterruptPhaseDestroying;
    }

    // Keep the handle and storage on any failure so a caller can retry teardown.
    status = DeviceInterruptDisable(interrupt);
    if (status != OS_EOK) {
        return status;
    }

    if (interrupt->State == NULL) {
        return OS_EOK;
    }
    
    if (interrupt->State->ProviderReserved) {
        status = __ProviderControl(interrupt, InterruptProviderRelease);
        if (status != OS_EOK) {
            return status;
        }
        interrupt->State->ProviderReserved = 0;
    }
    
    status = Syscall_InterruptRelease(interrupt->State->Handle);
    if (status != OS_EOK) {
        ERROR("DeviceInterruptDestroy: Syscall_InterruptRelease allocation %u failed: %u; state retained",
              interrupt->State->Handle, status);
        return status;
    }
    
    free(interrupt->Vectors);
    free(interrupt->State);
    interrupt->State = NULL;
    interrupt->Vectors = NULL;
    interrupt->Count = 0;
    interrupt->Kind = InterruptSourceInvalid;
    return OS_EOK;
}
