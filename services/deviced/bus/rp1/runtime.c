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

#include <ddk/types/interrupt-group.h>
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
    const struct FdtRp1Interrupt* interrupt;
    struct PciDmaDescription      dma;
    struct Rp1Child*              child;
    oserr_t                       status;
    uint8_t                       used[FDT_RP1_INTERRUPT_COUNT] = { 0 };

    TRACE("__Rp1ValidateChildren(bus=%p, children=%u)", bus, bus->ChildCount);
    for (child = bus->Children; child != NULL; child = child->Next) {
        status = Rp1GetDmaDescription(bus->Parent, child->Firmware.NodeOffset, &dma);
        if (status != OS_EOK) {
            ERROR("__Rp1ValidateChildren: Rp1GetDmaDescription node %u failed: %u",
                  child->Firmware.NodeOffset, status);
            return status;
        }
        
        // DMA requires a registered host before a child can access memory.
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
            
            // Before allocating runtime state, check that the source uses the
            // RP1 controller and its number fits the interrupt table.
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
            
            if (used[interrupt->Number]) {
                // Each source can be used by only one child, so two drivers cannot
                // configure or release the same interrupt.
                WARNING("__Rp1ValidateChildren: node %u duplicates source %u",
                        child->Firmware.NodeOffset, interrupt->Number);
                return OS_ENOTSUPPORTED;
            }
            
            used[interrupt->Number] = 1;
        }
    }
    return OS_EOK;
}

oserr_t
Rp1RuntimePrepareRemoval(
    _InOut_ struct Rp1Bus* bus)
{
    struct Rp1Child* child;

    // Before removal, drivers must finish restoring Ethernet registers and
    // release all DMA references. Keep requests available until cleanup has
    // removed the saved Ethernet state, even if the MAC has already stopped.
    for (child = bus->Children; child != NULL; child = child->Next) {
        if (child->Ethernet != NULL || child->Resources.DmaReferences != 0) {
            return OS_EBUSY;
        }
    }

    TRACE("Rp1RuntimePrepareRemoval(bus=%p)", bus);
    if (bus->Runtime == NULL) {
        return OS_EOK;
    }

    // Every child must release its interrupts before removal can block new
    // reservations.
    for (unsigned int i = 0; i < FDT_RP1_INTERRUPT_COUNT; i++) {
        if (bus->Runtime->Sources[i].Owner != NULL) {
            TRACE("Rp1RuntimePrepareRemoval: source %u kept by device %u, allocation %u, principal %u",
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

    // Children must release their sources before the runtime can stop.
    // Once they are released, block new reservations before stopping delivery.
    status = Rp1RuntimePrepareRemoval(bus);
    if (status != OS_EOK) {
        TRACE("Rp1RuntimeStop: Rp1RuntimePrepareRemoval bus %p failed: %u", bus, status);
        return status;
    }
    bus->Ready = 0;

    // Stop PCI message delivery before removing interrupt vectors and shared
    // mappings. Release the kernel group last. If a step fails, keep the
    // remaining resources so a later stop can retry the cleanup sequence.
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
    const struct FdtRp1Interrupt*interrupt;
    InterruptGroupResult_t       result;
    struct Rp1InterruptParent    parent;
    struct Rp1Runtime*           runtime;
    struct Rp1Child*             child;
    oserr_t                      status;
    oserr_t                      cleanup;
    unsigned int                 i;

    InterruptGroupRequest_t request = {
        .Minimum = 1,
        .Optimal = 1,
        .Kind = InterruptSourceMessage
    };
    
    TRACE("Rp1RuntimeStart(bus=%p, ready=%d)", bus, bus->Ready);
    // A ready runtime can be reused while it accepts requests. If shutdown has
    // started, finish it before allocating resources or registering callbacks.
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

    // Firmware and DMA settings must be valid before PCI bus mastering allows
    // devices to access memory.
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

    for (child = bus->Children; child != NULL; child = child->Next) {
        for (i = 0; i < child->Firmware.InterruptCount; i++) {
            interrupt = &child->Firmware.Interrupts[i];
            
            // All child interrupts must be configured before Ready allows
            // drivers to use the runtime.
            status = Rp1InterruptConfigure(
                &runtime->Controller,
                interrupt->Number,
                interrupt->Type
            );
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
        ERROR("Rp1RuntimeStart: Rp1RuntimeStop bus %p failed during rollback: %u (setup status %u); runtime kept",
              bus, cleanup, status);
    }
    return status;
}
