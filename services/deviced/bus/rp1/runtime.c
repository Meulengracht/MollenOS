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

#include <bus/rp1/dma.h>
#include <bus/pci/device.h>
#include <bus/pci/host-private.h>
#include <internal/_syscalls.h>
#include <ddk/utils.h>
#include <stdlib.h>

#include "runtime-private.h"

static oserr_t
__Rp1ValidateChildren(
    _In_ struct Rp1Bus* bus)
{
    uint8_t claimed[FDT_RP1_INTERRUPT_COUNT] = { 0 };

    TRACE("__Rp1ValidateChildren(bus=%p, children=%u)", bus, bus->ChildCount);
    for (struct Rp1Child* child = bus->Children; child != NULL; child = child->Next) {
        const struct FdtRp1Interrupt* interrupt;
        struct PciDmaDescription      dma;
        oserr_t                       status;
        
        status = Rp1GetDmaDescription(bus->Parent, child->Firmware.NodeOffset, &dma);
        if (status != OS_EOK) {
            ERROR("__Rp1ValidateChildren: Rp1GetDmaDescription node %u failed: %u",
                  child->Firmware.NodeOffset, status);
            return status;
        }
        
        // A child needs a registered DMA host to transfer data directly to or
        // from memory.
        if (dma.Host.HostId == UUID_INVALID) {
            WARNING("__Rp1ValidateChildren: node %u has no registered DMA host", child->Firmware.NodeOffset);
            return OS_ENOTSUPPORTED;
        }
        
        if (dma.CachePolicy == DmDmaCacheUnknown || dma.Map.Count == 0) {
            WARNING("__Rp1ValidateChildren: node %u has unsupported DMA cache policy %u or empty map (%u ranges)",
                    child->Firmware.NodeOffset, dma.CachePolicy, dma.Map.Count);
            return OS_ENOTSUPPORTED;
        }
        
        for (unsigned int i = 0; i < child->Firmware.InterruptCount; i++) {
            interrupt = &child->Firmware.Interrupts[i];
            // Check that this interrupt belongs to the RP1 controller and fits
            // its interrupt table before allocating runtime state.
            if (interrupt->Controller != child->Firmware.Controller ||
                interrupt->Number >= FDT_RP1_INTERRUPT_COUNT) {
                WARNING("__Rp1ValidateChildren: node %u interrupt %u has controller %u, source %u (expected controller %u)",
                        child->Firmware.NodeOffset, i, interrupt->Controller,
                        interrupt->Number, child->Firmware.Controller);
                return OS_EINVALPARAMS;
            }
            
            if (interrupt->Type != 1 && interrupt->Type != 4) {
                WARNING("__Rp1ValidateChildren: node %u source %u has unsupported type %u",
                        child->Firmware.NodeOffset, interrupt->Number, interrupt->Type);
                return OS_ENOTSUPPORTED;
            }
            
            if (claimed[interrupt->Number]) {
                // Each RP1 interrupt can belong to only one child. Reject
                // duplicates so two children cannot configure or release it.
                WARNING("__Rp1ValidateChildren: node %u duplicates source %u",
                        child->Firmware.NodeOffset, interrupt->Number);
                return OS_ENOTSUPPORTED;
            }
            
            claimed[interrupt->Number] = 1;
        }
    }
    return OS_EOK;
}

oserr_t
Rp1RuntimePrepareRemoval(
    _InOut_ struct Rp1Bus* bus)
{
    TRACE("Rp1RuntimePrepareRemoval(bus=%p)", bus);
    if (bus->Runtime == NULL) {
        return OS_EOK;
    }
    // Block new interrupt reservations before checking whether a child still
    // owns one.
    for (unsigned int i = 0; i < FDT_RP1_INTERRUPT_COUNT; i++) {
        if (bus->Runtime->Sources[i].Owner != NULL) {
            TRACE("Rp1RuntimePrepareRemoval: source %u retained by device %u, allocation %u, principal %u",
                  i, bus->Runtime->Sources[i].Owner->DeviceId,
                  bus->Runtime->Sources[i].Allocation, bus->Runtime->Sources[i].Principal);
            return OS_EBUSY;
        }
    }
    bus->Runtime->Quiescing = 1;
    return OS_EOK;
}

oserr_t
Rp1RuntimeStop(
    _InOut_ struct Rp1Bus* bus)
{
    struct Rp1Runtime* runtime = bus->Runtime;
    oserr_t            status;

    TRACE("Rp1RuntimeStop(bus=%p, runtime=%p)", bus, runtime);
    if (runtime == NULL) {
        return OS_EOK;
    }

    // Stop new reservations first. If a child still owns a source, keep the
    // runtime active so the child can release it.
    status = Rp1RuntimePrepareRemoval(bus);
    if (status != OS_EOK) {
        TRACE("Rp1RuntimeStop: Rp1RuntimePrepareRemoval bus %p failed: %u", bus, status);
        return status;
    }
    bus->Ready = 0;

    // Stop PCI interrupt messages before removing interrupt vectors or shared mappings.
    status = Rp1PciMessagesQuiesce(runtime);
    if (status != OS_EOK) {
        ERROR("Rp1RuntimeStop: Rp1PciMessagesQuiesce bus %p failed: %u", bus, status);
        return status;
    }
    
    status = Rp1InterruptDestroy(&runtime->Controller);
    if (status != OS_EOK) {
        ERROR("Rp1RuntimeStop: Rp1InterruptDestroy bus %p failed: %u", bus, status);
        return status;
    }
    
    status = Rp1CompletionsStop(runtime);
    if (status != OS_EOK) {
        ERROR("Rp1RuntimeStop: Rp1CompletionsStop bus %p failed: %u", bus, status);
        return status;
    }
    
    status = Rp1PciMessagesStop(runtime);
    if (status != OS_EOK) {
        ERROR("Rp1RuntimeStop: Rp1PciMessagesStop bus %p failed: %u", bus, status);
        return status;
    }
    
    if (runtime->Pci.Allocation != UUID_INVALID) {
        status = Syscall_InterruptRelease(runtime->Pci.Allocation);
        if (status != OS_EOK) {
            ERROR("Rp1RuntimeStop: Syscall_InterruptRelease allocation %u failed: %u",
                  runtime->Pci.Allocation, status);
            return status;
        }
        runtime->Pci.Allocation = UUID_INVALID;
    }
    free(runtime);
    
    bus->Runtime = NULL;
    TRACE("Rp1RuntimeStop: bus %p runtime released", bus);
    return OS_EOK;
}

oserr_t
Rp1RuntimeStart(
    _InOut_ struct Rp1Bus* bus)
{
    InterruptGroupResult_t    result;
    struct Rp1InterruptParent parent;
    struct Rp1Runtime*        runtime;
    oserr_t                   status;
    oserr_t                   cleanup;

    InterruptGroupRequest_t request = {
        .Minimum = 1,
        .Optimal = 1,
        .Strategies = INTERRUPT_STRATEGY_MSIX
    };
    
    TRACE("Rp1RuntimeStart(bus=%p, ready=%d)", bus, bus->Ready);
    // Reuse a ready runtime while it accepts requests. Stop a runtime that is
    // shutting down before allocating resources and registering callbacks again.
    if (bus->Ready && !bus->Runtime->Quiescing) {
        return OS_EOK;
    }
    
    status = Rp1RuntimeStop(bus);
    if (status != OS_EOK) {
        WARNING("Rp1RuntimeStart: Rp1RuntimeStop bus %p failed: %u", bus, status);
        return status;
    }
    
    if (bus->Parent->Host->InterruptControllerId == UUID_INVALID) {
        WARNING("Rp1RuntimeStart: bus %p host has no message controller", bus);
        return OS_ENOTSUPPORTED;
    }
    
    status = __Rp1ValidateChildren(bus);
    if (status != OS_EOK) {
        WARNING("Rp1RuntimeStart: __Rp1ValidateChildren bus %p failed: %u", bus, status);
        return status;
    }

    // Check firmware and DMA settings before enabling PCI bus mastering, which
    // lets devices transfer data directly to or from memory.
    runtime = calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        ERROR("Rp1RuntimeStart: runtime allocation failed for bus %p", bus);
        return OS_EOOM;
    }
    bus->Runtime = runtime;
    runtime->Bus = bus;
    request.ControllerID = bus->Parent->Host->InterruptControllerId;
    
    // Point unused MSI-X entries at one disabled message. Each child driver
    // sets up its own interrupt group and kernel handler.
    status = Syscall_InterruptAllocate(&request, &result);
    if (status != OS_EOK) {
        ERROR("Rp1RuntimeStart: Syscall_InterruptAllocate controller %u failed: %u",
              request.ControllerID, status);
        goto error;
    }
    
    runtime->Pci.Allocation = result.Handle;
    
    status = Rp1PciMessagesStart(runtime);
    if (status != OS_EOK) {
        ERROR("Rp1RuntimeStart: Rp1PciMessagesStart allocation %u failed: %u",
              runtime->Pci.Allocation, status);
        goto error;
    }

    parent = (struct Rp1InterruptParent){
        .Context = runtime,
        .VectorCount = FDT_RP1_INTERRUPT_COUNT,
        .Mask = Rp1SourceMask,
        .Unmask = Rp1SourceUnmask
    };
    
    // Set up RP1 routing only after PCI message delivery is ready. Keep each
    // interrupt disabled until its routing is configured.
    status = Rp1InterruptInitialize(
        &runtime->Controller,
        &runtime->Pci.Apbs,
        &parent
    );
    if (status != OS_EOK) {
        ERROR("Rp1RuntimeStart: Rp1InterruptInitialize bus %p failed: %u", bus, status);
        goto error;
    }

    for (struct Rp1Child* child = bus->Children; child != NULL; child = child->Next) {
        for (unsigned int i = 0; i < child->Firmware.InterruptCount; i++) {
            const struct FdtRp1Interrupt* interrupt = &child->Firmware.Interrupts[i];
            
            // Set up all child interrupts before marking the runtime ready, so
            // drivers cannot use it while setup is still in progress.
            status = Rp1InterruptConfigure(&runtime->Controller, interrupt->Number, interrupt->Type);
            if (status != OS_EOK) {
                ERROR("Rp1RuntimeStart: Rp1InterruptConfigure node %u, source %u, type %u failed: %u",
                      child->Firmware.NodeOffset, interrupt->Number, interrupt->Type, status);
                goto error;
            }
            
            if (interrupt->Type == 4) {
                // Register the completion callback for level-triggered
                // interrupts before a driver can request one.
                status = Rp1CompletionStart(runtime, interrupt->Number);
                if (status != OS_EOK) {
                    ERROR("Rp1RuntimeStart: Rp1CompletionStart node %u, source %u failed: %u",
                          child->Firmware.NodeOffset, interrupt->Number, status);
                    goto error;
                }
            }
        }
    }

    bus->Ready = 1;
    TRACE("Rp1RuntimeStart: bus %p ready, allocation %u, children %u",
          bus, runtime->Pci.Allocation, bus->ChildCount);
    return OS_EOK;

error:
    cleanup = Rp1RuntimeStop(bus);
    if (cleanup != OS_EOK) {
        ERROR("Rp1RuntimeStart: Rp1RuntimeStop bus %p failed during rollback: %u (setup status %u); runtime retained",
              bus, cleanup, status);
    }
    return status;
}
