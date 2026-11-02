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
        WARNING("PlatformInterruptQuery: device %u has invalid query buffer %p or length %zu",
                entry->DeviceId, buffer, length);
        return OS_EINVALPARAMS;
    }

    if (query->Kind || query->ControllerID) {
        WARNING("PlatformInterruptQuery: device %u source %u has kind %u or controller %u set",
                entry->DeviceId, query->Index, query->Kind, query->ControllerID);
        return OS_EINVALPARAMS;
    }

    if (query->Line.Line || query->Line.Pin || query->Line.AcpiConform) {
        WARNING("PlatformInterruptQuery: device %u source %u has line %d, pin %d or flags %u set",
                entry->DeviceId, query->Index, query->Line.Line,
                query->Line.Pin, query->Line.AcpiConform);
        return OS_EINVALPARAMS;
    }

    if (!PlatformIsActivationReady(entry)) {
        WARNING("PlatformInterruptQuery: device %u is not ready for source %u",
                entry->DeviceId, query->Index);
        return OS_EBUSY;
    }

    if (query->Index >= entry->Firmware.InterruptCount) {
        WARNING("PlatformInterruptQuery: device %u source %u exceeds interrupt count %u",
                entry->DeviceId, query->Index, entry->Firmware.InterruptCount);
        return OS_ENOTSUPPORTED;
    }

    // Return one selected source. Querying must not activate or reserve it,
    // and failure leaves the caller's request untouched.
    result.Index = query->Index;
    result.Kind = InterruptSourceFixedLine;

    source = &entry->Firmware.Interrupts[query->Index];

    status = __PlatformInterruptIdentity(source, &result.ControllerID);
    if (status != OS_EOK) {
        ERROR("PlatformInterruptQuery: device %u source %u controller %u line %u identity failed (%u)",
              entry->DeviceId, query->Index, source->Controller, source->Number, status);
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
        WARNING("__PlatformInterruptReserve: device %u source %u exceeds interrupt count %u",
                entry->DeviceId, request->Index, entry->Firmware.InterruptCount);
        return OS_EINVALPARAMS;
    }

    if (!PlatformIsActivationReady(entry)) {
        WARNING("__PlatformInterruptReserve: device %u is not ready for source %u",
                entry->DeviceId, request->Index);
        return OS_EBUSY;
    }

    status = PlatformActivationControl(entry, principal, DevicePrepare);
    if (status != OS_EOK) {
        ERROR("__PlatformInterruptReserve: device %u source %u preparation for principal %u failed (%u)",
              entry->DeviceId, request->Index, principal, status);
        return status;
    }

    lease = &entry->Interrupts[request->Index];
    if (lease->Allocation != UUID_INVALID &&
        (lease->Allocation != request->Allocation || lease->Principal != principal)) {
        WARNING("__PlatformInterruptReserve: device %u source %u allocation %u principal %u conflicts with allocation %u principal %u",
                entry->DeviceId, request->Index, request->Allocation, principal,
                lease->Allocation, lease->Principal);
        return OS_EBUSY;
    }

    for (uint32_t i = 0; i < entry->Firmware.InterruptCount; i++) {
        if (i != request->Index && entry->Interrupts[i].Allocation == request->Allocation) {
            WARNING("__PlatformInterruptReserve: device %u source %u allocation %u already uses source %u",
                    entry->DeviceId, request->Index, request->Allocation, i);
            return OS_EINVALPARAMS;
        }
    }

    status = __PlatformInterruptIdentity(
        &entry->Firmware.Interrupts[request->Index],
        &attach.ControllerID
    );
    if (status != OS_EOK) {
        ERROR("__PlatformInterruptReserve: device %u source %u controller %u line %u identity failed (%u)",
              entry->DeviceId, request->Index,
              entry->Firmware.Interrupts[request->Index].Controller,
              entry->Firmware.Interrupts[request->Index].Number, status);
        return status;
    }
    attach.Line = entry->Firmware.Interrupts[request->Index].Number;
    attach.LineFlags = INTERRUPT_ACPICONFORM_PRESENT | INTERRUPT_ACPICONFORM_FIXED |
        INTERRUPT_ACPICONFORM_SHAREABLE;

    if (entry->Firmware.Interrupts[request->Index].Type == 4) {
        attach.LineFlags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }

    status = DeviceInterruptProviderAttach(&attach);
    if (status != OS_EOK) {
        ERROR("__PlatformInterruptReserve: device %u source %u allocation %u controller %u line %u attachment failed (%u)",
              entry->DeviceId, request->Index, request->Allocation,
              attach.ControllerID, attach.Line, status);
        if (lease->Allocation != UUID_INVALID) {
            if (status == OS_EBUSY) {
                return OS_EUNKNOWN;
            }
            return status;
        }

        // If copying the reply fails, the kernel may already have attached the
        // line. Close the allocation before leaving its lease empty. If cleanup
        // cannot confirm the line is detached, keep the allocation ID so device
        // stop and removal remain blocked.
        cleanup = DeviceInterruptProviderClose(request->Allocation, principal, entry->DeviceId);
        if (cleanup != OS_EOK && cleanup != OS_ENOENT && cleanup != OS_EPERMISSIONS &&
            cleanup != OS_ENOTSUPPORTED) {
            ERROR("__PlatformInterruptReserve: device %u source %u allocation %u close failed; lease kept (%u)",
                  entry->DeviceId, request->Index, request->Allocation, cleanup);

            lease->Principal = principal;
            lease->Allocation = request->Allocation;
            if (status == OS_EBUSY) {
                return OS_EUNKNOWN;
            }
            return status;
        }
        return status;
    }

    lease->Principal = principal;
    lease->Allocation = request->Allocation;
    lease->Attached = 1;
    return OS_EOK;
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
        WARNING("PlatformInterruptControl: device %u has invalid request buffer %p or length %zu",
                entry->DeviceId, buffer, length);
        return OS_EINVALPARAMS;
    }

    if (principal == UUID_INVALID || request->Allocation == UUID_INVALID) {
        WARNING("PlatformInterruptControl: device %u has invalid principal %u or allocation %u",
                entry->DeviceId, principal, request->Allocation);
        return OS_EPERMISSIONS;
    }

    TRACE("PlatformInterruptControl(device=%u, allocation=%u, operation=%u)",
          entry->DeviceId, request->Allocation, request->Operation);
    if (request->Operation > DeviceInterruptSourceRelease) {
        WARNING("PlatformInterruptControl: device %u allocation %u has invalid operation %u",
                entry->DeviceId, request->Allocation, request->Operation);
        return OS_EINVALPARAMS;
    }

    if (request->Operation == DeviceInterruptSourceReserve) {
        status = __PlatformInterruptReserve(entry, principal, request);
        if (status != OS_EOK) {
            return status;
        }
        return OS_EOK;
    }

    if (request->Index) {
        WARNING("PlatformInterruptControl: device %u allocation %u operation %u requires index 0, got %u",
                entry->DeviceId, request->Allocation, request->Operation, request->Index);
        return OS_EINVALPARAMS;
    }

    for (i = 0; i < entry->Firmware.InterruptCount; i++) {
        if (entry->Interrupts[i].Allocation == request->Allocation) {
            lease = &entry->Interrupts[i];
            break;
        }
    }

    if (lease != NULL && lease->Principal != principal) {
        WARNING("PlatformInterruptControl: device %u source %u allocation %u belongs to principal %u, not %u",
                entry->DeviceId, i, request->Allocation, lease->Principal, principal);
        return OS_EPERMISSIONS;
    }

    if (request->Operation == DeviceInterruptSourceDisable) {
        // The DDK disables and unbinds the kernel line synchronously after
        // this call. Direct GIC shared interrupts need no further controller
        // masking in deviced.
        return OS_EOK;
    }

    if (request->Operation == DeviceInterruptSourceEnable) {
        if (lease == NULL) {
            WARNING("PlatformInterruptControl: device %u allocation %u has no lease to enable",
                    entry->DeviceId, request->Allocation);
            return OS_ENOENT;
        }

        status = DeviceInterruptProviderCheckEnabled(
            request->Allocation,
            principal,
            entry->DeviceId
        );
        if (status != OS_EOK) {
            ERROR("PlatformInterruptControl: device %u source %u allocation %u enabled check failed (%u)",
                  entry->DeviceId, i, request->Allocation, status);
            return status;
        }
        return OS_EOK;
    }

    status = DeviceInterruptProviderClose(request->Allocation, principal, entry->DeviceId);
    if (status == OS_ENOENT) {
        TRACE("PlatformInterruptControl: device %u allocation %u is already closed",
              entry->DeviceId, request->Allocation);
        status = OS_EOK;
    }

    if (status != OS_EOK) {
        if (lease == NULL || lease->Attached) {
            ERROR("PlatformInterruptControl: device %u allocation %u close failed (%u)",
                  entry->DeviceId, request->Allocation, status);
            return status;
        }

        if (status != OS_EPERMISSIONS && status != OS_ENOTSUPPORTED) {
            ERROR("PlatformInterruptControl: device %u source %u pending allocation %u close failed (%u)",
                  entry->DeviceId, i, request->Allocation, status);
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
