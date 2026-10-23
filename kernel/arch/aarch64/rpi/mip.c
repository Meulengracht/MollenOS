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

#include <arch/interrupts.h>
#include <component/ic.h>
#include <ddk/io.h>
#include <debug.h>
#include <heap.h>

#include "../common/device.h"
#include "mip.h"

struct __MipController {
    struct Arm64MipRoute    Route;
    uintptr_t               Registers;
    struct __MipController* Next;
};

static uint32_t
__MipCapacity(
    _In_ void* context)
{
    struct __MipController* controller = context;

    // Firmware may assign only part of the physical MIP to this host. Advertise
    // that validated slice, not the total number of inputs on the chip.
    return controller->Route.Count;
}

static oserr_t
__MipAllocate(
    _In_    void*                          context,
    _In_    uint32_t                       ordinal,
    _InOut_ InterruptControllerResource_t* resource)
{
    struct __MipController* controller = context;

    // The ordinal is the driver's zero-based message number within this host's
    // slice. The GIC ID identifies its CPU handler; the token is the number the
    // device writes to the MIP. Both advance together from the validated route.
    // The core checks the ordinal and owns the slot claim, so no separate MIP
    // allocation bitmap is needed here.
    resource->TableIndex = controller->Route.FirstInterrupt + ordinal;
    resource->Line = (int)resource->TableIndex;
    resource->Token = controller->Route.MessageOffset + ordinal;
    resource->Message.Address = controller->Route.MessageAddress;
    resource->Message.Data = (uint32_t)resource->Token;
    return OS_EOK;
}

static oserr_t
__MipSetEnabled(
    _In_ void*                                context,
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  enable)
{
    (void)context;
    // Every MIP message has its own shared GIC interrupt. Enable or mask that
    // GIC input rather than changing the MIP's bank-wide setup. The GIC also
    // keeps pending delivery across a disable/enable cycle and acknowledges it.
    return InterruptConfigure(resource, enable);
}

static oserr_t
__MipRelease(
    _In_ void*                                context,
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  retire)
{
    // There are no per-message hardware mappings to release. The core removes
    // or retires the slot only after delivery and readers have been stopped.
    (void)context;
    (void)resource;
    (void)retire;
    return OS_EOK;
}

static oserr_t
__ValidateDistinctController(
    _In_ const struct __MipController* controllers,
    _In_ const struct Arm64MipRoute*   route)
{
    const struct Arm64MipRoute* previous;

    // Separate MIP blocks must not claim overlapping GIC inputs or CPU
    // registers. Hosts naming the same provider intentionally share hardware;
    // their individual slot claims are coordinated by the interrupt core.
    while (controllers) {
        previous = &controllers->Route;
        if (previous->Provider != route->Provider) {
            if (previous->FirstInterrupt < route->FirstInterrupt + route->Count &&
                route->FirstInterrupt < previous->FirstInterrupt + previous->Count) {
                return OS_EINVALPARAMS;
            }
            if (previous->RegisterBase < route->RegisterBase + ARM64_MIP_REGISTER_LENGTH &&
                route->RegisterBase < previous->RegisterBase + ARM64_MIP_REGISTER_LENGTH) {
                return OS_EINVALPARAMS;
            }
        }
        controllers = controllers->Next;
    }
    return OS_EOK;
}

static void
__MipWrite(
    _In_ uintptr_t registers,
    _In_ uint32_t  offset,
    _In_ uint32_t  value)
{
    // Order device writes before later memory accesses. Ordinary memory stores
    // do not provide the ordering required for hardware register setup.
    WriteVolatileMemory((volatile void*)(registers + offset), &value, sizeof(value));
    mb();
}

static oserr_t
__InitializeMip(
    _In_    struct __MipController* controllers,
    _InOut_ struct __MipController* controller)
{
    struct __MipController* previous;
    uintptr_t              registers;
    uint32_t               readback;

    // Hosts can share a physical MIP. Earlier list entries have already been
    // initialized, so reuse their mapping instead of resetting shared hardware.
    for (previous = controllers; previous != controller; previous = previous->Next) {
        if (previous->Route.Provider == controller->Route.Provider) {
            controller->Registers = previous->Registers;
            return OS_EOK;
        }
    }
    registers = Arm64MapDevice(controller->Route.RegisterBase, ARM64_MIP_REGISTER_LENGTH);
    if (!registers) {
        return OS_EOOM;
    }
    controller->Registers = registers;

    // GIC initialization has left every shared interrupt masked. First block
    // MIP delivery to both the CPU and the video processor, then configure all
    // message inputs to send an edge rather than hold an interrupt level.
    // Keep video processor delivery blocked: these messages
    // belong to kernel drivers, not to the firmware running on that processor.
    __MipWrite(registers, MIP_HOST_MASK_LOW, MIP_ALL_BITS);
    __MipWrite(registers, MIP_HOST_MASK_HIGH, MIP_ALL_BITS);
    __MipWrite(registers, MIP_VPU_MASK_LOW, MIP_ALL_BITS);
    __MipWrite(registers, MIP_VPU_MASK_HIGH, MIP_ALL_BITS);
    __MipWrite(registers, MIP_CONFIG_LOW, MIP_ALL_BITS);
    __MipWrite(registers, MIP_CONFIG_HIGH, MIP_ALL_BITS);

    // Allow CPU delivery at the MIP. Individual GIC inputs remain masked until
    // drivers enable them. Read back the last mask to check that setup reached
    // the device before any controller becomes visible to callers.
    __MipWrite(registers, MIP_HOST_MASK_LOW, MIP_NO_BITS);
    __MipWrite(registers, MIP_HOST_MASK_HIGH, MIP_NO_BITS);
    ReadVolatileMemory((const volatile void*)(registers + MIP_HOST_MASK_HIGH), &readback, sizeof(readback));
    mb();
    return readback == MIP_NO_BITS ? OS_EOK : OS_EUNKNOWN;
}

oserr_t
Arm64MipInitialize(
    _In_ const DeviceTree_t* tree,
    _In_ uint32_t            gicId,
    _In_ uint32_t            interruptCount)
{
    const DeviceTreeNode_t* node;
    const DeviceTreeNode_t* gic;
    struct Arm64MipRoute    route;
    struct __MipController* controllers = NULL;
    struct __MipController* controller;
    struct __MipController* next;
    uuid_t                  id;
    oserr_t                 status;

    struct InterruptMessageBackend backend = {
        .Operations = {
            .Capacity = __MipCapacity,
            .AllocateOrdinal = __MipAllocate,
            .SetEnabled = __MipSetEnabled,
            .Release = __MipRelease
        }
    };
    
    InterruptMessageControllerIdentity_t identity = {
        .Version = 1,
        .Kind = InterruptMessageControllerKindDeviceTree
    };

    gic = DeviceTreeFindPhandle(tree, gicId);
    if (!gic) {
        return OS_EINVALPARAMS;
    }

    // Validate every host before accessing device registers or publishing
    // controller IDs. A bad later host must not leave earlier ones usable
    // while we are still deciding whether the firmware description is safe.
    for (node = tree->Nodes; node; node = node->NextNode) {
        status = Arm64MipReadRoute(tree, node, gic, interruptCount, &route);
        if (status == OS_ENOTSUPPORTED) {
            continue;
        }
        if (status != OS_EOK) {
            ERROR("Arm64MipInitialize: invalid host %s MSI route: %u", node->Name, status);
            goto fail;
        }
        
        status = __ValidateDistinctController(controllers, &route);
        if (status != OS_EOK) {
            ERROR("Arm64MipInitialize: overlapping MIP resources for %s", node->Name);
            goto fail;
        }
        
        controller = kmalloc(sizeof(struct __MipController));
        if (!controller) {
            status = OS_EOOM;
            goto fail;
        }
        
        memset(controller, 0, sizeof(struct __MipController));
        controller->Route = route;
        controller->Next = controllers;

        controllers = controller;
    }

    // Only now map and configure the hardware. No driver can allocate a message
    // yet, and each physical MIP is configured once even if hosts share it.
    for (controller = controllers; controller; controller = controller->Next) {
        status = __InitializeMip(controllers, controller);
        if (status != OS_EOK) {
            goto fail;
        }
    }

    // Publish one controller identity per PCI host. Successful publication
    // transfers lifetime responsibility to the interrupt subsystem; remove that
    // context from our cleanup list, but keep its route and mapping permanently.
    while (controllers) {
        controller = controllers;
        backend.Context = controller;
        identity.FirmwareNode = controller->Route.Host->NodeOffset;
        
        status = CreateInterruptMessageController(&identity, &backend, &id);
        if (status != OS_EOK) {
            goto fail;
        }
        
        controllers = controller->Next;
        controller->Next = NULL;
        DEBUG("MIP host=%s controller=%u first=%u count=%u address=0x%llx",
            controller->Route.Host->Name, id, controller->Route.FirstInterrupt,
            controller->Route.Count, (unsigned long long)controller->Route.MessageAddress);
    }
    return OS_EOK;

fail:
    // Only unpublished contexts remain on this list. Device mappings persist
    // for the life of the kernel; published controllers are not rolled back.
    // The caller must treat this initialization failure as fatal to boot.
    while (controllers) {
        next = controllers->Next;
        kfree(controllers);
        controllers = next;
    }
    return status;
}
