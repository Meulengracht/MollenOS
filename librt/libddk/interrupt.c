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
#include <ddk/interrupt_request.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Keeps kernel ownership separate from editable driver configuration.
 * Bound can remain set after a failed Enable cleanup. Retaining the handle and
 * this state allows Disable or Destroy to retry without losing any mappings.
 */
struct DeviceInterruptState {
    uuid_t Handle;
    int    Bound;
    int    Enabled;
};

void
DeviceInterruptInitialize(
    _Out_ DeviceInterrupt_t* interrupt,
    _In_  BusDevice_t*       device)
{
    // Initialization allocates nothing, so even a driver without a bus device
    // can prepare a line descriptor before requesting its reservation.
    memset(interrupt, 0, sizeof(*interrupt));
    interrupt->DeviceId = UUID_INVALID;
    interrupt->Line.Line = INTERRUPT_NONE;
    interrupt->Line.Pin = INTERRUPT_NONE;
    if (device != NULL) {
        interrupt->DeviceId = device->Base.Id;
        interrupt->Line.Line = device->InterruptLine;
        interrupt->Line.Pin = device->InterruptPin;
        interrupt->Line.AcpiConform = device->InterruptAcpiConform;
    }
}

oserr_t
DeviceInterruptAllocate(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _In_    uint32_t           strategies,
    _Out_   uint32_t*          countOut)
{
    InterruptAllocationRequest_t request;
    InterruptAllocationResult_t  result;
    struct DeviceInterruptState* state;
    DeviceInterruptVector_t*      vectors;
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
    if (min != 1 || !(strategies & INTERRUPT_STRATEGY_INTx)) {
        return OS_ENOTSUPPORTED;
    }

    // Reserve bookkeeping before the system call, following the DMA wrappers'
    // ownership pattern. No allocation can become hidden by a later malloc failure.
    state = calloc(1, sizeof(*state));
    vectors = calloc(1, sizeof(*vectors));
    if (state == NULL || vectors == NULL) {
        free(state);
        free(vectors);
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
        free(vectors);
        free(state);
        return status;
    }
    state->Handle = result.Handle;
    vectors[0].AffinityMask = UINT32_MAX;
    vectors[0].ResourceTable.HandleResource = UUID_INVALID;
    interrupt->State = state;
    interrupt->Vectors = vectors;
    interrupt->Count = result.Count;
    interrupt->Strategy = result.Strategy;
    *countOut = result.Count;
    return OS_EOK;
}

/**
 * @brief Prevents edits to configuration already copied into the kernel.
 * Checking Bound also covers failed enablement whose cleanup needs a retry.
 */
static oserr_t
__GetEditableVector(
    _In_  DeviceInterrupt_t*        interrupt,
    _In_  uint32_t                  index,
    _Out_ DeviceInterruptVector_t** vectorOut)
{
    if (interrupt == NULL || interrupt->State == NULL || index >= interrupt->Count) {
        return OS_EINVALPARAMS;
    }
    if (interrupt->State->Bound || interrupt->State->Enabled) {
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
    oserr_t status;

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
    oserr_t status;

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
    uuid_t handle;
    oserr_t status;

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
    oserr_t status;
    unsigned int i;

    // Keep stable resource positions because fast handlers address them by index.
    status = __GetEditableVector(interrupt, index, &vector);
    if (status != OS_EOK) {
        return status;
    }
    if (ioSpace == NULL) {
        return OS_EINVALPARAMS;
    }
    for (i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
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
    DeviceInterruptVector_t* vector;
    FastInterruptMemoryResource_t* resource;
    oserr_t status;
    unsigned int i;

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
    for (i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
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
    InterruptBindRequest_t        request;
    struct DeviceInterruptState* state;
    oserr_t                      status;
    oserr_t                      cleanup;

    if (interrupt == NULL || interrupt->State == NULL) {
        return OS_EINVALPARAMS;
    }
    state = interrupt->State;
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
    if (interrupt->Vectors[0].ResourceTable.Handler == NULL) {
        return OS_EINVALPARAMS;
    }
    // INTx has one vector. Bind it before enabling delivery, including when the
    // physical line is already enabled by another device sharing this interrupt.
    if (!state->Bound) {
        request.Index = 0;
        request.Resources = interrupt->Vectors[0].ResourceTable;
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
    // A successful disable waits for running handlers. Only then is it safe to
    // release their mappings or allow setters to change the configuration.
    if (state->Enabled) {
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
    return OS_EOK;
}

oserr_t
DeviceInterruptDestroy(
    _InOut_ DeviceInterrupt_t* interrupt)
{
    oserr_t status;

    // Keep the handle and storage on any failure so a caller can retry teardown.
    status = DeviceInterruptDisable(interrupt);
    if (status != OS_EOK || interrupt->State == NULL) {
        return status;
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
