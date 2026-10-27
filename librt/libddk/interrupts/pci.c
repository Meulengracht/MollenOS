/**
 * Copyright 2026, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include <ddk/interrupts/pci.h>
#include <ddk/utils.h>
#include <internal/_syscalls.h>
#include <os/device.h>
#include <string.h>

#include "private.h"

oserr_t
__InterruptPciReserve(
    _In_    DeviceInterrupt_t*        interrupt,
    _In_    uint32_t                  minimum,
    _In_    uint32_t                  optimal,
    _In_    uint32_t                  modes,
    _Out_   InterruptGroupResult_t*   result,
    _Out_   uint32_t*                 selectedMode)
{
    const uint32_t preference[] = {
        INTERRUPT_STRATEGY_MSIX,
        INTERRUPT_STRATEGY_MSI,
        INTERRUPT_STRATEGY_INTx
    };

    struct PciInterruptSupport support = { 0 };
    InterruptGroupRequest_t    request;
    oserr_t                    status = OS_ENOTSUPPORTED;

    if (modes == 0 || (modes & ~INTERRUPT_STRATEGY_ANY)) {
        return OS_EINVALPARAMS;
    }

    if (modes & (INTERRUPT_STRATEGY_MSI | INTERRUPT_STRATEGY_MSIX)) {
        status = OSDeviceIOCtl(
            interrupt->DeviceId,
            OSIOCTLREQUEST_PCI_INTERRUPT_QUERY,
            &support,
            sizeof(support)
        );
        if (status != OS_EOK && status != OS_ENOTSUPPORTED) {
            ERROR("__InterruptPciReserve: device %u capability query failed: %u", interrupt->DeviceId, status);
            return status;
        }
        if (status == OS_ENOTSUPPORTED) {
            memset(&support, 0, sizeof(support));
        }
    }
    
    modes &= support.Strategies | INTERRUPT_STRATEGY_INTx;

    // PCI policy lives above kernel reservation. A one-message MSI request and
    // a one-vector MSI-X request use the same kernel resource, but different
    // device programming. Never retry transport/configuration errors as INTx.
    status = OS_ENOTSUPPORTED;
    for (uint32_t i = 0; i < sizeof(preference) / sizeof(preference[0]); i++) {
        uint32_t capacity;
        uint32_t mode = preference[i];
        
        if (!(modes & mode)) {
            continue;
        }

        capacity = mode == INTERRUPT_STRATEGY_MSIX ? support.MsixCapacity : 1;
        if (capacity < minimum) {
            continue;
        }
        
        memset(&request, 0, sizeof(request));
        request.Flags = interrupt->Flags;
        request.Minimum = minimum;
        request.Optimal = optimal < capacity ? optimal : capacity;
        
        if (mode == INTERRUPT_STRATEGY_INTx) {
            request.Kind = InterruptSourceLine;
            request.Line = interrupt->Line;
        } else {
            request.Kind = InterruptSourceMessage;
            request.ControllerID = support.ControllerID;
        }
        
        status = Syscall_InterruptAllocate(&request, result);
        TRACE("__InterruptPciReserve: device %u mode 0x%x reservation: %u", interrupt->DeviceId, mode, status);
        if (status == OS_EOK) {
            *selectedMode = mode;
            return OS_EOK;
        }
        
        if (status != OS_ENOTSUPPORTED && status != OS_EBUSY) {
            return status;
        }
    }
    return status;
}

oserr_t
__InterruptPciControl(
    _In_ DeviceInterrupt_t*                interrupt,
    _In_ enum __InterruptProviderOperation operation)
{
    struct PciInterruptRequest request = {
        .Allocation = interrupt->State->Handle,
        .Strategy = interrupt->State->PciMode
    };

    // Only this module knows the PCI wire operations. Local ownership is
    // recorded by the shared lifecycle before sending any uncertain RPC.
    switch (operation) {
        case InterruptProviderReserve:
            request.Operation = PciInterruptReserve;
            request.Count = interrupt->Count;
            break;
        case InterruptProviderEnable:
            request.Operation = PciInterruptEnable;
            break;
        case InterruptProviderDisable:
            request.Operation = PciInterruptDisable;
            break;
        case InterruptProviderRelease:
            request.Operation = PciInterruptRelease;
            break;
        default:
            return OS_EINVALPARAMS;
    }
    
    return OSDeviceIOCtl(
        interrupt->DeviceId,
        OSIOCTLREQUEST_PCI_INTERRUPT,
        &request,
        sizeof(request)
    );
}

oserr_t
__InterruptPciProgram(
    _In_ DeviceInterrupt_t* interrupt)
{
    struct PciInterruptRequest request = {
        .Operation = PciInterruptProgram,
        .Allocation = interrupt->State->Handle,
        .Strategy = interrupt->State->PciMode
    };

    DeviceInterruptMessage_t     message;
    struct DeviceInterruptState* state = interrupt->State;

    // Program only after reservation succeeded. A failed write or lost reply
    // leaves provider ownership with the shared lifecycle for orderly rollback.
    for (uint32_t i = 0; i < interrupt->Count; i++) {
        oserr_t status = Syscall_InterruptGetMessage(state->Handle, i, &message);
        if (status != OS_EOK) {
            ERROR("__InterruptPciProgram: allocation %u message %u query failed: %u", state->Handle, i, status);
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
            sizeof(request)
        );
        if (status != OS_EOK) {
            ERROR("__InterruptPciProgram: device %u allocation %u PCI vector %u programming failed: %u",
                  interrupt->DeviceId, state->Handle, i, status);
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
DeviceInterruptAllocatePci(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _In_    uint32_t           modes,
    _Out_   uint32_t*          countOut)
{
    if (countOut != NULL) {
        *countOut = 0;
    }

    if (interrupt == NULL || !interrupt->IsPci) {
        return OS_EINVALPARAMS;
    }
    
    return __InterruptAllocate(interrupt, min, optimal, modes, countOut);
}

uint32_t
DeviceInterruptGetPciMode(
    _In_ const DeviceInterrupt_t* interrupt)
{
    // Keep capability details out of the common object's public result fields.
    return interrupt->State == NULL ? 0 : interrupt->State->PciMode;
}

