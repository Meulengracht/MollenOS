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
 */

//#define __TRACE

#include <bus/pci/device.h>
#include <bus/pci/host-private.h>
#include <ddk/utils.h>
#include <string.h>

#include "runtime-private.h"

#define RP1_VECTOR_BYTES   16U
#define RP1_VECTOR_CONTROL 12U

oserr_t
Rp1SourceSetTableMask(
    _InOut_ struct Rp1Runtime* runtime,
    _In_    unsigned int       source,
    _In_    int                mask)
{
    size_t   offset = source * RP1_VECTOR_BYTES + RP1_VECTOR_CONTROL;
    uint32_t value;
    uint32_t actual;
    oserr_t  status;

    // Change only the mask bit. Keep the MSI-X address, data, and other
    // settings unchanged.
    value = (uint32_t)ReadDeviceIo(&runtime->Pci.Table, offset, 4);
    value = mask ? value | 1U : value & ~1U;
    status = WriteDeviceIo(&runtime->Pci.Table, offset, value, 4);
    if (status != OS_EOK) {
        ERROR("Rp1SourceSetTableMask: WriteDeviceIo source %u, offset 0x%zx, mask %d failed: %u",
              source, offset, mask, status);
        return status;
    }

    actual = (uint32_t)ReadDeviceIo(&runtime->Pci.Table, offset, 4);
    if (actual != value) {
        ERROR("Rp1SourceSetTableMask: source %u, offset 0x%zx readback 0x%x, expected 0x%x",
              source, offset, actual, value);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

oserr_t
Rp1SourceMask(
    _In_ void*        context,
    _In_ unsigned int source)
{
    return Rp1SourceSetTableMask(context, source, 1);
}

oserr_t
Rp1SourceUnmask(
    _In_ void*        context,
    _In_ unsigned int source)
{
    return Rp1SourceSetTableMask(context, source, 0);
}

static struct Rp1Source*
__Rp1ChildSource(
    _In_ struct Rp1Child* child,
    _In_ unsigned int    index)
{
    // The index must identify an interrupt in the child's firmware description.
    // Its interrupt number selects the RP1 source table entry.
    return &child->Bus->Runtime->Sources[child->Firmware.Interrupts[index].Number];
}

static int
__Rp1PendingCloseFinished(
    _In_ oserr_t status)
{
    if (status == OS_EOK || status == OS_ENOENT) {
        return 1;
    }
    // These errors happen before attachment succeeds, so this caller has no
    // callbacks to release.
    return status == OS_EPERMISSIONS || status == OS_ENOTSUPPORTED;
}

static oserr_t
__Rp1ProgramSource(
    _InOut_ struct Rp1Child* child,
    _In_    uint32_t         index)
{
    struct Rp1Runtime* runtime = child->Bus->Runtime;
    struct Rp1Source*  source = __Rp1ChildSource(child, index);

    uint32_t number = child->Firmware.Interrupts[index].Number;
    uint32_t words[3];
    uint32_t actual;
    size_t   offset;
    oserr_t  status;

    if (source->Programmed) {
        return OS_EOK;
    }

    if (!source->MessageReady || runtime->Controller.Enabled[number]) {
        return OS_EBUSY;
    }

    // The caller holds Bus->Lock. Use the kernel attachment's destination and
    // keep the source masked until all message words have been verified.
    status = Rp1SourceSetTableMask(runtime, number, 1);
    if (status != OS_EOK) {
        return status;
    }

    words[0] = source->Message.AddressLow;
    words[1] = source->Message.AddressHigh;
    words[2] = source->Message.Data;

    for (uint32_t i = 0; i < 3; i++) {
        offset = number * RP1_VECTOR_BYTES + i * sizeof(uint32_t);

        status = WriteDeviceIo(&runtime->Pci.Table, offset, words[i], 4);
        if (status != OS_EOK) {
            ERROR("__Rp1ProgramSource: device %u source %u word %u write failed: %u",
                  child->DeviceId, number, i, status);
            return status;
        }

        actual = (uint32_t)ReadDeviceIo(&runtime->Pci.Table, offset, 4);
        if (actual != words[i]) {
            ERROR("__Rp1ProgramSource: source %u word %u readback 0x%x, expected 0x%x",
                  number, i, actual, words[i]);
            return OS_EUNKNOWN;
        }
    }
    source->Programmed = 1;
    return OS_EOK;
}

static oserr_t
__Rp1ReserveSource(
    _InOut_ struct Rp1Child*                           child,
    _In_    uuid_t                                     principal,
    _In_    const struct DeviceInterruptSourceRequest* request)
{
    struct InterruptProviderVector vector = { 0 };
    struct Rp1Source*              source;
    struct Rp1Source*              other;
    oserr_t                        status;
    oserr_t                        cleanup;

    struct InterruptProviderAttachRequest attachment = {
        .Allocation = request->Allocation,
        .Principal = principal,
        .Device = child->DeviceId,
        .ControllerID = child->Bus->Parent->Host->InterruptControllerId,
        .Count = 1,
        .Vectors = &vector
    };

    if (request->Index >= child->Firmware.InterruptCount) {
        return OS_EINVALPARAMS;
    }

    source = __Rp1ChildSource(child, request->Index);
    if (source->Owner != NULL) {
        if (source->Owner != child || source->Allocation != request->Allocation ||
            source->Principal != principal) {
            return OS_EBUSY;
        }
    }

    // An allocation may attach to only one source. Check the child's other
    // sources before attaching so a retry cannot redirect an existing group.
    for (uint32_t i = 0; i < child->Firmware.InterruptCount; i++) {
        other = __Rp1ChildSource(child, i);
        if (i != request->Index && other->Owner == child &&
            other->Allocation == request->Allocation) {
            return OS_EINVALPARAMS;
        }
    }

    vector.Completion = child->Bus->Runtime->Completions[
        child->Firmware.Interrupts[request->Index].Number].Handle;

    status = DeviceInterruptProviderAttach(&attachment);
    if (status != OS_EOK) {
        WARNING("__Rp1ReserveSource: device %u source %u allocation %u attach failed: %u",
                child->DeviceId, request->Index, request->Allocation, status);

        if (source->Owner != NULL && source->MessageReady) {
            return status == OS_EBUSY ? OS_EUNKNOWN : status;
        }

        // Copying the result to userspace can fail after attachment succeeds.
        // Close that attachment before clearing ownership. If close fails,
        // leave the source recorded so Release can retry the cleanup.
        cleanup = DeviceInterruptProviderClose(
            request->Allocation,
            principal,
            child->DeviceId
        );
        if (__Rp1PendingCloseFinished(cleanup)) {
            memset(source, 0, sizeof(*source));
            return status;
        }

        ERROR("__Rp1ReserveSource: allocation %u close failed: %u; ownership kept",
              request->Allocation, cleanup);
    }

    source->Owner = child;
    source->Allocation = request->Allocation;
    source->Principal = principal;

    if (status == OS_EOK) {
        source->Message = vector.Message;
        source->MessageReady = 1;
        status = __Rp1ProgramSource(child, request->Index);
    }

    // EBUSY is reserved for an untouched reservation. After attachment the
    // client must still perform cleanup, even if programming could not finish.
    return status == OS_EBUSY ? OS_EUNKNOWN : status;
}

oserr_t
Rp1RuntimeControl(
    _InOut_ struct Rp1Child*                      child,
    _In_    uuid_t                               principal,
    _In_    struct DeviceInterruptSourceRequest* request)
{
    struct Rp1Runtime* runtime = child->Bus->Runtime;
    struct Rp1Source*  source = NULL;
    struct Rp1Source*  candidate;
    uint32_t           index = 0;
    uint32_t           number;
    oserr_t            status;
    oserr_t            cleanup;

    TRACE("Rp1RuntimeControl(device=%u, allocation=%u, operation=%u, source=%u)",
          child->DeviceId, request->Allocation, request->Operation, request->Index);

    if (principal == UUID_INVALID || request->Allocation == UUID_INVALID) {
        return OS_EPERMISSIONS;
    }

    if (request->Operation > DeviceInterruptSourceRelease) {
        return OS_EINVALPARAMS;
    }

    if (request->Operation != DeviceInterruptSourceReserve && request->Index != 0) {
        return OS_EINVALPARAMS;
    }

    if (runtime == NULL || !child->Bus->Ready || runtime->Quiescing) {
        return OS_EBUSY;
    }

    if (request->Operation == DeviceInterruptSourceReserve) {
        return __Rp1ReserveSource(child, principal, request);
    }

    // After Reserve, find the source by allocation and verify its owner before
    // changing interrupt delivery. The request index cannot select a source.
    for (uint32_t i = 0; i < child->Firmware.InterruptCount; i++) {
        candidate = __Rp1ChildSource(child, i);
        if (candidate->Owner == child && candidate->Allocation == request->Allocation) {
            if (candidate->Principal != principal) {
                WARNING("Rp1RuntimeControl: principal %u does not own allocation %u",
                        principal, request->Allocation);
                return OS_EPERMISSIONS;
            }
            source = candidate;
            index = i;
            break;
        }
    }

    if (source == NULL) {
        if (request->Operation == DeviceInterruptSourceDisable) {
            return OS_EOK;
        }
        if (request->Operation == DeviceInterruptSourceRelease) {
            status = DeviceInterruptProviderClose(
                request->Allocation,
                principal,
                child->DeviceId
            );
            return status == OS_ENOENT ? OS_EOK : status;
        }
        return OS_ENOENT;
    }

    number = child->Firmware.Interrupts[index].Number;
    if (request->Operation == DeviceInterruptSourceEnable) {
        if (!source->Programmed) {
            return OS_EBUSY;
        }

        status = DeviceInterruptProviderCheckEnabled(
            request->Allocation,
            principal,
            child->DeviceId
        );
        if (status != OS_EOK) {
            return status;
        }

        // Exact Enable retries must not reconfigure an already active source.
        if (runtime->Controller.Enabled[number]) {
            return OS_EOK;
        }

        // Kernel handlers and completion resources must be ready before the
        // first upstream write can arrive. A failed unmask still requires stop.
        status = Rp1InterruptEnable(&runtime->Controller, number);
        if (status != OS_EOK) {
            cleanup = Rp1InterruptDisable(&runtime->Controller, number);
            ERROR("Rp1RuntimeControl: source %u enable failed: %u; stop result %u",
                  number, status, cleanup);
        }
        return status;
    }

    status = Rp1InterruptDisable(&runtime->Controller, number);
    if (status != OS_EOK) {
        ERROR("Rp1RuntimeControl: source %u stop failed: %u", number, status);
        return status;
    }

    if (request->Operation == DeviceInterruptSourceDisable) {
        return OS_EOK;
    }

    // Closing verifies that the child has disabled and unbound its handlers.
    // Keep ownership of the message and completion until that synchronization
    // succeeds. Clearing the source earlier could race an in-flight handler.
    status = DeviceInterruptProviderClose(
        request->Allocation,
        principal,
        child->DeviceId
    );
    if (status != OS_EOK && status != OS_ENOENT) {
        if (source->MessageReady || !__Rp1PendingCloseFinished(status)) {
            WARNING("Rp1RuntimeControl: allocation %u close failed: %u",
                    request->Allocation, status);
            return status;
        }
    }
    memset(source, 0, sizeof(struct Rp1Source));
    return OS_EOK;
}
