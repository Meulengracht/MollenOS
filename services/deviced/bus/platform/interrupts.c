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

#include <ddk/types/interrupt-controller.h>
#include <ddk/types/interrupt-source.h>
#include <ddk/interrupts/provider.h>
#include <ddk/utils.h>
#include <string.h>

#include "private.h"

static oserr_t
__PlatformInterruptIdentity(
    _In_  const struct PlatformInterrupt* source,
    _Out_ uuid_t*                         controller)
{
    InterruptLineSourceIdentity_t identity = {
        .Version = 1,
        .FirmwareController = source->Controller,
        .Line = source->Number
    };

    // The kernel must resolve the device-tree phandle to the active interrupt
    // controller and check that the line is within its registered range.
    return DeviceInterruptLineSourceResolve(&identity, controller);
}

oserr_t
PlatformInterruptQuery(
    _In_    struct PlatformEntry* entry,
    _InOut_ void*                 buffer,
    _In_    size_t                length)
{
    struct DeviceInterruptSourceQuery* query = buffer;
    struct DeviceInterruptSourceQuery  result = { 0 };
    const struct PlatformInterrupt*    source;
    oserr_t                            status;

    if (query == NULL || length != sizeof(*query)) {
        return OS_EINVALPARAMS;
    }

    if (query->Kind || query->ControllerID) {
        return OS_EINVALPARAMS;
    }

    if (query->Line.Line || query->Line.Pin || query->Line.AcpiConform) {
        return OS_EINVALPARAMS;
    }

    if (!PlatformIsActivationReady(entry)) {
        return OS_EBUSY;
    }

    if (query->Index >= entry->Firmware.InterruptCount) {
        return OS_ENOTSUPPORTED;
    }

    // Return one selected source. Querying must not activate or reserve it,
    // and failure leaves the caller's request untouched.
    result.Index = query->Index;
    result.Kind = InterruptSourceFixedLine;

    source = &entry->Firmware.Interrupts[query->Index];

    status = __PlatformInterruptIdentity(source, &result.ControllerID);
    if (status != OS_EOK) {
        WARNING("PlatformInterruptQuery: device %u source %u identity failed: %u",
                entry->DeviceId, query->Index, status);
        return status;
    }

    result.Line.Line = (int)source->Number;
    result.Line.Pin = INTERRUPT_NONE;
    result.Line.AcpiConform = INTERRUPT_ACPICONFORM_PRESENT |
        INTERRUPT_ACPICONFORM_FIXED | INTERRUPT_ACPICONFORM_SHAREABLE;
    if (source->Type == 4) {
        result.Line.AcpiConform |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }

    *query = result;
    TRACE("PlatformInterruptQuery: device %u source %u controller %u line %d",
          entry->DeviceId, result.Index, result.ControllerID, result.Line.Line);
    return OS_EOK;
}

static oserr_t
__PlatformInterruptReserve(
    _InOut_ struct PlatformEntry*                      entry,
    _In_    uuid_t                                     principal,
    _In_    const struct DeviceInterruptSourceRequest* request)
{
    struct InterruptProviderVector        vector = { 0 };
    struct PlatformInterruptLease*        lease;
    struct InterruptProviderAttachRequest attach = {
        .Allocation = request->Allocation,
        .Principal = principal,
        .Device = entry->DeviceId,
        .Count = 1,
        .Vectors = &vector
    };

    oserr_t  status;
    oserr_t  cleanup;

    TRACE("__PlatformInterruptReserve(device=%u, allocation=%u, source=%u)",
          entry->DeviceId, request->Allocation, request->Index);
    if (request->Index >= entry->Firmware.InterruptCount) {
        return OS_EINVALPARAMS;
    }

    if (!PlatformIsActivationReady(entry)) {
        return OS_EBUSY;
    }

    status = PlatformActivationControl(entry, principal, DevicePrepare);
    if (status != OS_EOK) {
        return status;
    }

    lease = &entry->Interrupts[request->Index];
    if (lease->Allocation != UUID_INVALID &&
        (lease->Allocation != request->Allocation || lease->Principal != principal)) {
        return OS_EBUSY;
    }

    for (uint32_t i = 0; i < entry->Firmware.InterruptCount; i++) {
        if (i != request->Index && entry->Interrupts[i].Allocation == request->Allocation) {
            return OS_EINVALPARAMS;
        }
    }

    status = __PlatformInterruptIdentity(
        &entry->Firmware.Interrupts[request->Index],
        &attach.ControllerID
    );
    if (status != OS_EOK) {
        return status;
    }
    attach.Line = entry->Firmware.Interrupts[request->Index].Number;
    attach.LineFlags = INTERRUPT_ACPICONFORM_PRESENT | INTERRUPT_ACPICONFORM_FIXED |
        INTERRUPT_ACPICONFORM_SHAREABLE;

    if (entry->Firmware.Interrupts[request->Index].Type == 4) {
        attach.LineFlags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }

    status = DeviceInterruptProviderAttach(&attach);
    if (status == OS_EOK) {
        lease->Principal = principal;
        lease->Allocation = request->Allocation;
        lease->Attached = 1;
        return OS_EOK;
    }

    WARNING("__PlatformInterruptReserve: device %u source %u attachment failed: %u",
            entry->DeviceId, request->Index, status);
    if (lease->Allocation != UUID_INVALID) {
        return status == OS_EBUSY ? OS_EUNKNOWN : status;
    }

    // If copying the reply fails, the kernel may already have attached the
    // line. Close the allocation before leaving its lease empty. If cleanup
    // cannot confirm the line is detached, keep the allocation ID so device
    // stop and removal remain blocked.
    cleanup = DeviceInterruptProviderClose(request->Allocation, principal, entry->DeviceId);
    if (cleanup != OS_EOK && cleanup != OS_ENOENT && cleanup != OS_EPERMISSIONS &&
        cleanup != OS_ENOTSUPPORTED) {
        ERROR("__PlatformInterruptReserve: allocation %u close failed: %u; lease kept",
              request->Allocation, cleanup);

        lease->Principal = principal;
        lease->Allocation = request->Allocation;
        return status == OS_EBUSY ? OS_EUNKNOWN : status;
    }
    return status;
}

oserr_t
PlatformInterruptControl(
    _InOut_ struct PlatformEntry* entry,
    _In_    uuid_t                principal,
    _In_    void*                 buffer,
    _In_    size_t                length)
{
    const struct DeviceInterruptSourceRequest* request = buffer;
    struct PlatformInterruptLease*             lease = NULL;
    oserr_t                                    status;
    uint32_t                                   i;

    if (buffer == NULL || length != sizeof(*request)) {
        return OS_EINVALPARAMS;
    }

    if (principal == UUID_INVALID || request->Allocation == UUID_INVALID) {
        return OS_EPERMISSIONS;
    }

    TRACE("PlatformInterruptControl(device=%u, allocation=%u, operation=%u)",
          entry->DeviceId, request->Allocation, request->Operation);
    if (request->Operation > DeviceInterruptSourceRelease) {
        return OS_EINVALPARAMS;
    }

    if (request->Operation == DeviceInterruptSourceReserve) {
        return __PlatformInterruptReserve(entry, principal, request);
    }

    if (request->Index) {
        return OS_EINVALPARAMS;
    }

    for (i = 0; i < entry->Firmware.InterruptCount; i++) {
        if (entry->Interrupts[i].Allocation == request->Allocation) {
            lease = &entry->Interrupts[i];
            break;
        }
    }

    if (lease != NULL && lease->Principal != principal) {
        return OS_EPERMISSIONS;
    }

    if (request->Operation == DeviceInterruptSourceDisable) {
        // The DDK disables and unbinds the kernel line synchronously after
        // this call. Direct GIC shared interrupts need no further controller
        // masking in deviced.
        return OS_EOK;
    }

    if (request->Operation == DeviceInterruptSourceEnable) {
        return lease == NULL ? OS_ENOENT : DeviceInterruptProviderCheckEnabled(
            request->Allocation,
            principal,
            entry->DeviceId
        );
    }

    status = DeviceInterruptProviderClose(request->Allocation, principal, entry->DeviceId);
    if (status == OS_ENOENT) {
        status = OS_EOK;
    }

    if (status != OS_EOK) {
        if (lease == NULL || lease->Attached) {
            return status;
        }

        if (status != OS_EPERMISSIONS && status != OS_ENOTSUPPORTED) {
            return status;
        }
        // OS_EBUSY can occur before owner and allocation-kind checks during
        // attachment and cleanup. These later errors confirm that the pending
        // lease never attached. An attached lease requires a successful close.
        TRACE("PlatformInterruptControl: allocation %u pending attachment rejected: %u; lease released",
              request->Allocation, status);
        status = OS_EOK;
    }

    if (lease != NULL) {
        memset(lease, 0, sizeof(*lease));
    }
    return status;
}
