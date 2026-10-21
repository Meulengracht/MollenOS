/**
 * MollenOS
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
 *
 *
 * Interrupt Support Definitions & Structures
 * - This header describes the base interrupt-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/busdevice.h>
#include <ddk/types/interrupt.h>
#include <ddk/types/pci-interrupt.h>
#include <os/device.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <stdlib.h>
#include <string.h>

// Local state in libddk to keep track of changes
struct DeviceInterruptState {
    uuid_t Handle;
    int    Bound;
    int    Enabled;
    int    PciReserved;
    int    Ready;
    int    PendingCleanup;
};

static oserr_t
__PciControl(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ uint32_t           operation)
{
    struct PciInterruptRequest request = {
        .Operation = operation,
        .Allocation = interrupt->State->Handle,
        .Strategy = interrupt->Strategy
    };

    return OSDeviceIOCtl(
        interrupt->DeviceId,
        OSIOCTLREQUEST_PCI_INTERRUPT,
        &request,
        sizeof(request)
    );
}

static oserr_t
__ReservePciMessages(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    struct DeviceInterruptState* state = interrupt->State;
    DeviceInterruptMessage_t     message;
    oserr_t                      status;
    struct PciInterruptRequest   request = {
        .Operation = PciInterruptReserve,
        .Allocation = state->Handle,
        .Strategy = interrupt->Strategy,
        .Count = interrupt->Count
    };

    // A lost reply can hide a successful reservation. Retain its cleanup duty.
    state->PciReserved = 1;
    status = OSDeviceIOCtl(
        interrupt->DeviceId, OSIOCTLREQUEST_PCI_INTERRUPT, &request, sizeof(request)
    );
    if (status != OS_EOK) {
        // Busy means another reservation owns the untouched function.
        if (status == OS_EBUSY) {
            state->PciReserved = 0;
        }
        return status;
    }

    request.Operation = PciInterruptProgram;
    request.Count = 0;
    
    for (uint32_t i = 0; i < interrupt->Count; i++) {
        status = Syscall_InterruptGetMessage(state->Handle, i, &message);
        if (status != OS_EOK) {
            return status;
        }

        request.Index = i;
        request.AddressLow = message.AddressLow;
        request.AddressHigh = message.AddressHigh;
        request.Data = message.Data;
        status = OSDeviceIOCtl(
            interrupt->DeviceId,
            OSIOCTLREQUEST_PCI_INTERRUPT,
            &request,
            sizeof(struct PciInterruptRequest)
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

void
DeviceInterruptInitialize(
    _Out_ DeviceInterrupt_t* interrupt,
    _In_  BusDevice_t*       device)
{
    // Initialization allocates nothing, so even a driver without a bus device
    // can prepare a line descriptor before requesting its reservation.
    memset(interrupt, 0, sizeof(DeviceInterrupt_t));
    interrupt->DeviceId = UUID_INVALID;
    interrupt->Line.Line = INTERRUPT_NONE;
    interrupt->Line.Pin = INTERRUPT_NONE;
    
    if (device != NULL) {
        interrupt->DeviceId = device->Base.Id;
        interrupt->IsPci = device->Type == BusTypePci;
        interrupt->Line.Line = device->InterruptLine;
        interrupt->Line.Pin = device->InterruptPin;
        interrupt->Line.AcpiConform = device->InterruptAcpiConform;
    }
}

static oserr_t
__AllocateVectors(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    uint32_t i;

    interrupt->Vectors = calloc(interrupt->Count, sizeof(DeviceInterruptVector_t));
    if (interrupt->Vectors == NULL) {
        return OS_EOOM;
    }

    for (i = 0; i < interrupt->Count; i++) {
        interrupt->Vectors[i].AffinityMask = UINT32_MAX;
        interrupt->Vectors[i].ResourceTable.HandleResource = UUID_INVALID;
    }
    return OS_EOK;
}

oserr_t
DeviceInterruptAllocate(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _In_    uint32_t           strategies,
    _Out_   uint32_t*          countOut)
{
    InterruptGroupRequest_t      request;
    InterruptGroupResult_t       result;
    struct PciInterruptSupport   support = { 0 };
    struct DeviceInterruptState* state;
    oserr_t                      status;

    if (countOut == NULL) {
        return OS_EINVALPARAMS;
    }
    
    *countOut = 0;
    
    if (interrupt == NULL || min == 0 || min > optimal) {
        return OS_EINVALPARAMS;
    }
    if (strategies == 0 || (strategies & ~INTERRUPT_STRATEGY_ANY)) {
        return OS_EINVALPARAMS;
    }
    if (interrupt->Flags & ~INTERRUPT_EXCLUSIVE) {
        return OS_EINVALPARAMS;
    }
    if (interrupt->State != NULL) {
        return OS_EBUSY;
    }
    
    if ((strategies & (INTERRUPT_STRATEGY_MSI | INTERRUPT_STRATEGY_MSIX)) && interrupt->IsPci) {
        status = OSDeviceIOCtl(interrupt->DeviceId, OSIOCTLREQUEST_PCI_INTERRUPT_QUERY,
                              &support, sizeof(support));
        // Fall back only when a mode is absent or unsupported. Report broken
        // configuration and transport errors to the caller.
        if (status != OS_EOK && status != OS_ENOTSUPPORTED) {
            return status;
        }
        if (status != OS_EOK) {
            support.Strategies = 0;
        }
        strategies &= support.Strategies | INTERRUPT_STRATEGY_INTx;
    } else {
        strategies &= INTERRUPT_STRATEGY_INTx;
    }
    if (min != 1) {
        strategies &= INTERRUPT_STRATEGY_MSIX;
    }
    if (strategies & INTERRUPT_STRATEGY_MSIX) {
        if (support.MsixCapacity < min) {
            strategies &= ~INTERRUPT_STRATEGY_MSIX;
        } else if (optimal > support.MsixCapacity) {
            optimal = support.MsixCapacity;
        }
    }
    
    if (strategies == 0) {
        return OS_ENOTSUPPORTED;
    }

    // Keep a place for the handle before reserving. Vector storage is sized from
    // the negotiated count; if it fails, State retains any cleanup obligation.
    state = calloc(1, sizeof(struct DeviceInterruptState));
    if (state == NULL) {
        return OS_EOOM;
    }
    
    request.DeviceId = interrupt->DeviceId;
    request.Line = interrupt->Line;
    request.Flags = interrupt->Flags;
    request.Minimum = min;
    request.Optimal = optimal;
    request.Strategies = strategies;
    
    status = Syscall_InterruptAllocate(&request, &result);
    if (status != OS_EOK) {
        free(state);
        return status;
    }
    
    state->Handle = result.Handle;
    
    interrupt->State = state;
    interrupt->Count = result.Count;
    interrupt->Strategy = result.Strategy;

    status = __AllocateVectors(interrupt);
    if (status != OS_EOK) {
        goto rollback;
    }

    if (result.Strategy != INTERRUPT_STRATEGY_INTx) {
        status = __ReservePciMessages(interrupt);
        if (status != OS_EOK) {
            goto rollback;
        }
    }
    state->Ready = 1;
    
    *countOut = result.Count;
    return OS_EOK;

rollback:
    // Destroy keeps the handle reachable if release itself fails.
    (void)DeviceInterruptDestroy(interrupt);
    return status;
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
    if (!interrupt->State->Ready) {
        return OS_EBUSY;
    }

    status = Syscall_InterruptGetMessage(interrupt->State->Handle, index, &message);
    if (status == OS_EOK) {
        *messageOut = message;
    }
    return status;
}

// Ensures we don't vectors that are already bound
static oserr_t
__GetEditableVector(
    _In_  DeviceInterrupt_t*        interrupt,
    _In_  uint32_t                  index,
    _Out_ DeviceInterruptVector_t** vectorOut)
{
    if (interrupt == NULL || interrupt->State == NULL || index >= interrupt->Count) {
        return OS_EINVALPARAMS;
    }
    if (!interrupt->State->Ready || interrupt->State->Bound || interrupt->State->Enabled) {
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

oserr_t
DeviceInterruptEnable(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    InterruptBindRequest_t       request;
    struct DeviceInterruptState* state;
    oserr_t                      status;
    oserr_t                      cleanup;

    if (interrupt == NULL || interrupt->State == NULL) {
        return OS_EINVALPARAMS;
    }

    state = interrupt->State;
    if (!state->Ready) {
        return OS_EBUSY;
    }
    
    if (state->PendingCleanup) {
        status = DeviceInterruptDisable(interrupt);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    if (state->Enabled) {
        return OS_EOK;
    }
    
    // A previous enable may have failed partway through cleanup. Finish that
    // cleanup before rebinding; some of its old mappings may already be gone.
    if (state->Bound) {
        status = DeviceInterruptDisable(interrupt);
        if (status != OS_EOK) {
            return status;
        }
    }

    for (uint32_t i = 0; i < interrupt->Count; i++) {
        if (interrupt->Vectors[i].ResourceTable.Handler == NULL) {
            return OS_EINVALPARAMS;
        }
    }
    
    // Bind every vector before allowing dispatch.
    for (uint32_t i = 0; i < interrupt->Count; i++) {
        request.Index = i;
        request.Resources = interrupt->Vectors[i].ResourceTable;
        
        status = Syscall_InterruptBind(state->Handle, &request);
        if (status != OS_EOK) {
            // A failed mapping may itself need cleanup. Preserve that obligation
            // when unbinding fails so Disable and Destroy can finish it later.
            cleanup = Syscall_InterruptUnbind(state->Handle);
            state->Bound = cleanup != OS_EOK;
            return status;
        }
        state->Bound = 1;
    }
    
    status = Syscall_InterruptEnable(state->Handle);
    if (status != OS_EOK) {
        cleanup = Syscall_InterruptUnbind(state->Handle);
        if (cleanup == OS_EOK) {
            state->Bound = 0;
        }
        return status;
    }
    
    state->Enabled = 1;
    if (state->PciReserved) {
        state->PendingCleanup = 1;
        status = __PciControl(interrupt, PciInterruptEnable);
        if (status != OS_EOK) {
            // PCI delivery may be enabled even if the response failed. Stop it before
            // unmapping handlers, and keep their resources if stopping fails.
            (void)DeviceInterruptDisable(interrupt);
            return status;
        }
        state->PendingCleanup = 0;
    }
    return OS_EOK;
}

oserr_t
DeviceInterruptDisable(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    struct DeviceInterruptState* state;
    oserr_t                      status;

    if (interrupt == NULL) {
        return OS_EINVALPARAMS;
    }

    state = interrupt->State;
    if (state == NULL) {
        return OS_EOK;
    }
    
    if (state->PciReserved) {
        // Make Enable retry an incomplete stop. Clear this only after PCI delivery
        // and kernel dispatch are stopped and the handler mappings are removed.
        state->PendingCleanup = 1;

        status = __PciControl(interrupt, PciInterruptDisable);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    // A successful disable waits for running handlers. Only then is it safe to
    // release their mappings or allow setters to change the configuration.
    if (state->Enabled || state->Bound) {
        status = Syscall_InterruptDisable(state->Handle);
        if (status != OS_EOK) {
            return status;
        }
        state->Enabled = 0;
    }
    
    if (state->Bound) {
        status = Syscall_InterruptUnbind(state->Handle);
        if (status != OS_EOK) {
            return status;
        }
        state->Bound = 0;
    }
    state->PendingCleanup = 0;
    return OS_EOK;
}

oserr_t
DeviceInterruptDestroy(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    oserr_t status;

    if (interrupt != NULL && interrupt->State != NULL &&
        interrupt->Strategy != INTERRUPT_STRATEGY_INTx) {
        // Teardown may restore PCI state before failing. Block Enable until
        // the caller finishes Destroy and allocates again.
        interrupt->State->Ready = 0;
    }

    // Keep the handle and storage on any failure so a caller can retry teardown.
    status = DeviceInterruptDisable(interrupt);
    if (status != OS_EOK || interrupt->State == NULL) {
        return status;
    }
    
    if (interrupt->State->PciReserved) {
        status = __PciControl(interrupt, PciInterruptRelease);
        if (status != OS_EOK) return status;
        interrupt->State->PciReserved = 0;
    }
    
    status = Syscall_InterruptRelease(interrupt->State->Handle);
    if (status != OS_EOK) {
        return status;
    }
    
    free(interrupt->Vectors);
    free(interrupt->State);
    interrupt->State = NULL;
    interrupt->Vectors = NULL;
    interrupt->Count = 0;
    interrupt->Strategy = 0;
    return OS_EOK;
}
