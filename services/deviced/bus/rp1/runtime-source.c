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
#include <bus/rp1/ethernet.h>

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
        // All users of this source share the table entry. Once programmed,
        // leave the message unchanged on repeated calls.
        return OS_EOK;
    }

    if (!source->MessageReady || runtime->Controller.Enabled[number]) {
        WARNING("__Rp1ProgramSource: device %u source %u is not ready to program "
                "(message ready %u, enabled %u)",
                child->DeviceId, number, source->MessageReady,
                runtime->Controller.Enabled[number]);
        return OS_EBUSY;
    }

    // The caller must hold Bus->Lock. Keep the source masked while writing and
    // verifying the message address and data returned by the kernel.
    status = Rp1SourceSetTableMask(runtime, number, 1);
    if (status != OS_EOK) {
        return status;
    }

    words[0] = source->Message.AddressLow;
    words[1] = source->Message.AddressHigh;
    words[2] = source->Message.Data;

    // Check each address and data word by reading it back after writing.
    // If a write or readback fails, keep the source masked and Programmed clear.
    // A retry must write all three words again.
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
    InterruptBusVector_t vector = { 0 };
    struct Rp1Source*    source;
    struct Rp1Source*    other;
    oserr_t              status;
    oserr_t              cleanup;

    InterruptBusAttachRequest_t attachment = {
        .Allocation = request->Allocation,
        .Principal = principal,
        .Device = child->DeviceId,
        .ControllerID = child->Bus->Parent->Host->InterruptControllerId,
        .Count = 1,
        .Vectors = &vector
    };

    // For GEM, the requesting process must prepare the device before reserving
    // a source. Its clock setup stays in use until the source is released.
    status = Rp1EthernetStatus(child, principal);
    if (status != OS_EOK) {
        return status;
    }

    if (request->Index >= child->Firmware.InterruptCount) {
        ERROR("__Rp1ReserveSource: device %u source index %u exceeds interrupt count %u",
              child->DeviceId, request->Index, child->Firmware.InterruptCount);
        return OS_EINVALPARAMS;
    }

    source = __Rp1ChildSource(child, request->Index);
    if (source->Owner != NULL) {
        if (source->Owner != child || source->Allocation != request->Allocation ||
            source->Principal != principal) {
            WARNING("__Rp1ReserveSource: device %u source index %u is already reserved "
                    "(allocation %u, principal %u)",
                    child->DeviceId, request->Index, source->Allocation, source->Principal);
            return OS_EBUSY;
        }
    }

    // An allocation may attach to only one source. Check the child's other
    // sources before attaching so a retry cannot redirect an existing group.
    for (uint32_t i = 0; i < child->Firmware.InterruptCount; i++) {
        other = __Rp1ChildSource(child, i);
        if (i != request->Index && other->Owner == child &&
            other->Allocation == request->Allocation) {
            ERROR("__Rp1ReserveSource: device %u allocation %u already uses source index %u, "
                  "cannot reserve index %u",
                  child->DeviceId, request->Allocation, i, request->Index);
            return OS_EINVALPARAMS;
        }
    }

    vector.Completion = child->Bus->Runtime->Completions[
        child->Firmware.Interrupts[request->Index].Number].Handle;

    status = DeviceInterruptBusAttach(&attachment);
    if (status != OS_EOK) {
        WARNING("__Rp1ReserveSource: device %u source %u allocation %u attach failed: %u",
                child->DeviceId, request->Index, request->Allocation, status);

        if (source->Owner != NULL && source->MessageReady) {
            if (status == OS_EBUSY) {
                return OS_EUNKNOWN;
            }
            return status;
        }

        // Attachment may have succeeded even if copying the result failed.
        // Close it before clearing the source. If cleanup fails, keep the source
        // recorded so DeviceInterruptSourceRelease can retry.
        cleanup = DeviceInterruptBusClose(
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

    // Return OS_EBUSY only when the reservation is unchanged. Once attached,
    // the caller must clean up even if programming the source failed.
    if (status != OS_EOK) {
        if (status == OS_EBUSY) {
            return OS_EUNKNOWN;
        }
        return status;
    }

    source->Message = vector.Message;
    source->MessageReady = 1;
    status = __Rp1ProgramSource(child, request->Index);
    if (status != OS_EOK) {
        if (status == OS_EBUSY) {
            return OS_EUNKNOWN;
        }
        return status;
    }
    return OS_EOK;
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
        WARNING("Rp1RuntimeControl: device %u has invalid principal %u or allocation %u",
                child->DeviceId, principal, request->Allocation);
        return OS_EPERMISSIONS;
    }

    if (request->Operation > DeviceInterruptSourceRelease) {
        ERROR("Rp1RuntimeControl: device %u allocation %u has invalid operation %u",
              child->DeviceId, request->Allocation, request->Operation);
        return OS_EINVALPARAMS;
    }

    if (request->Operation != DeviceInterruptSourceReserve && request->Index != 0) {
        ERROR("Rp1RuntimeControl: device %u allocation %u operation %u requires index zero, got %u",
              child->DeviceId, request->Allocation, request->Operation, request->Index);
        return OS_EINVALPARAMS;
    }

    if (runtime == NULL || !child->Bus->Ready || runtime->Quiescing) {
        WARNING("Rp1RuntimeControl: device %u allocation %u operation %u requires a ready runtime "
                "(missing %d, bus ready %d, quiescing %d)",
                child->DeviceId, request->Allocation, request->Operation,
                runtime == NULL, child->Bus->Ready, runtime != NULL && runtime->Quiescing);
        return OS_EBUSY;
    }

    if (request->Operation == DeviceInterruptSourceReserve) {
        return __Rp1ReserveSource(child, principal, request);
    }

    // After reservation, use the allocation to find the source and check the
    // calling process before changing delivery. Only reserve requests select
    // a source by index.
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
        // A kernel attachment may remain after the local source was cleared or
        // an attach reply was lost. Release must still close that attachment.
        if (request->Operation == DeviceInterruptSourceDisable) {
            return OS_EOK;
        }
        if (request->Operation == DeviceInterruptSourceRelease) {
            status = DeviceInterruptBusClose(
                request->Allocation,
                principal,
                child->DeviceId
            );
            if (status != OS_EOK) {
                if (status == OS_ENOENT) {
                    return OS_EOK;
                }
                ERROR("Rp1RuntimeControl: device %u allocation %u without a source could not close (%u)",
                      child->DeviceId, request->Allocation, status);
                return status;
            }
            return OS_EOK;
        }
        WARNING("Rp1RuntimeControl: device %u allocation %u operation %u has no reserved source",
                child->DeviceId, request->Allocation, request->Operation);
        return OS_ENOENT;
    }

    number = child->Firmware.Interrupts[index].Number;
    if (request->Operation == DeviceInterruptSourceEnable) {
        if (!source->Programmed) {
            WARNING("Rp1RuntimeControl: device %u allocation %u source %u is not programmed",
                    child->DeviceId, request->Allocation, number);
            return OS_EBUSY;
        }

        status = DeviceInterruptBusStatus(
            request->Allocation,
            principal,
            child->DeviceId
        );
        if (status != OS_EOK) {
            ERROR("Rp1RuntimeControl: device %u allocation %u source %u enabled check failed (%u)",
                  child->DeviceId, request->Allocation, number, status);
            return status;
        }

        // An enable retry must leave an enabled source unchanged.
        if (runtime->Controller.Enabled[number]) {
            return OS_EOK;
        }

        // Kernel handlers and bus callback resources must be ready before
        // enabling hardware delivery. If enabling fails, disable the source
        // to stop any interrupts it may already be sending.
        status = Rp1InterruptEnable(&runtime->Controller, number);
        if (status != OS_EOK) {
            cleanup = Rp1InterruptDisable(&runtime->Controller, number);
            ERROR("Rp1RuntimeControl: source %u enable failed: %u; stop result %u",
                  number, status, cleanup);
            return status;
        }
        return OS_EOK;
    }

    status = Rp1InterruptDisable(&runtime->Controller, number);
    if (status != OS_EOK) {
        ERROR("Rp1RuntimeControl: source %u stop failed: %u", number, status);
        return status;
    }

    if (request->Operation == DeviceInterruptSourceDisable) {
        return OS_EOK;
    }

    // The child must disable and unbind its handlers before closing. Keep the
    // source recorded until close succeeds so another request cannot reuse it
    // while a handler is still running.
    status = DeviceInterruptBusClose(
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
