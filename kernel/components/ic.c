/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * System Component Infrastructure 
 * - The Interrupt Controller component. This component has the task
 *   of mapping and managing interrupt controllers
 */

//#define __TRACE

#include <assert.h>
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <component/ic.h>
#include <debug.h>
#include <heap.h>
#include <interrupts.h>
#include <limits.h>
#include <machine.h>
#include <spinlock.h>
#include <string.h>

static Spinlock_t   g_controllerLock = OS_SPINLOCK_INIT;
static uuid_t       g_messageControllerIdGen;

// Kernel handlers are registered during platform startup, before message
// controllers are created. Legacy PIC lines still need a permanent route when
// there is no IOAPIC controller.
static SystemInterruptController_t g_localController = {
    .Type = SystemInterruptControllerLocal
};
static SystemInterruptController_t g_platformLineController = {
    .Type = SystemInterruptControllerLine
};

static uint32_t
__PlatformMessageCapacity(
    _In_ void* context)
{
    (void)context;
    return InterruptMessageVectorCount();
}

static oserr_t
__PlatformMessageReserve(
    _In_    void*                          context,
    _In_    uint32_t                       ordinal,
    _InOut_ InterruptControllerResource_t* resource)
{
    InterruptDescriptor_t descriptor = { 0 };
    oserr_t               status;

    (void)context;
    status = InterruptMessageVector(ordinal, &descriptor, &resource->Message);
    if (status != OS_EOK) {
        ERROR("__PlatformMessageReserve: InterruptMessageVector ordinal %u failed: %u",
              ordinal, status);
        return status;
    }
    resource->TableIndex = (uuid_t)descriptor.Line;
    resource->Line = descriptor.Line;
    return OS_EOK;
}

static oserr_t
__PlatformMessageSetEnabled(
    _In_ void*                                context,
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  enable)
{
    oserr_t status;

    (void)context;
    status = InterruptConfigure(resource, enable);
    if (status != OS_EOK) {
        ERROR("__PlatformMessageSetEnabled: InterruptConfigure index %u, enable %d failed: %u",
              resource->TableIndex, enable, status);
    }
    return status;
}

static oserr_t
__PlatformMessageRelease(
    _In_ void*                                context,
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  retire)
{
    // The platform has no separately allocated remapping resources. Its vector
    // claim, including permanent retirement, belongs to the common core.
    (void)context;
    (void)resource;
    (void)retire;
    return OS_EOK;
}

oserr_t
SystemInterruptsInitialize(void)
{
    const InterruptMessageControllerIdentity_t source = {
        .Version = 1,
        .Kind = InterruptMessageControllerKindPlatform
    };
    
    const struct InterruptMessageBackend backend = {
        .Operations = {
            .Capacity = __PlatformMessageCapacity,
            .AllocateOrdinal = __PlatformMessageReserve,
            .SetEnabled = __PlatformMessageSetEnabled,
            .Release = __PlatformMessageRelease
        }
    };

    oserr_t oserr;
    uuid_t  controllerId;

    TRACE("SystemInterruptsInitialize()");

    // Initialize the interrupt components of the system
    InitializeInterruptTable();
    InitializeInterruptHandlers();
    
    // Allow the architecture to initialize platform-specific interrupts
    oserr = PlatformInterruptInitialize();
    if (oserr != OS_EOK) {
        ERROR("SystemInterruptsInitialize: PlatformInterruptInitialize failed: %u", oserr);
        ArchProcessorHalt();
    }

    // If there are no interrupt message vectors, no further initialization is needed.
    if (InterruptMessageVectorCount() == 0) {
        TRACE("SystemInterruptsInitialize: no platform message vectors");
        return OS_EOK;
    }

    oserr = CreateInterruptMessageController(&source, &backend, &controllerId);
    if (oserr != OS_EOK) {
        ERROR("SystemInterruptsInitialize: CreateInterruptMessageController failed: %u", oserr);
        return oserr;
    }
    TRACE("SystemInterruptsInitialize: platform message controller %u", controllerId);
    return OS_EOK;
}

static void
__AppendInterruptController(
    _In_ SystemInterruptController_t* controller)
{
    SystemInterruptController_t** link;

    // iterate to end of list
    for (link = &GetMachine()->InterruptController; *link != NULL; link = &(*link)->Link) { }

    // append it
    *link = controller;
}

oserr_t
CreateInterruptController(
    _In_ uuid_t    id,
    _In_ int       interruptLineBase,
    _In_ int       numberOfInterrupts,
    _In_ uintptr_t baseAddress)
{
    SystemInterruptController_t* controller;

    TRACE("CreateInterruptController(id=%u, line=%d, count=%d, address=0x%" PRIxIN ")",
          id, interruptLineBase, numberOfInterrupts, baseAddress);
    if (interruptLineBase < 0 || numberOfInterrupts <= 0) {
        WARNING("CreateInterruptController: invalid line range %d, count %d",
                interruptLineBase, numberOfInterrupts);
        return OS_EINVALPARAMS;
    }
    if (numberOfInterrupts - 1 > INT_MAX - interruptLineBase) {
        WARNING("CreateInterruptController: line range overflows at %d, count %d",
                interruptLineBase, numberOfInterrupts);
        return OS_EINVALPARAMS;
    }

    controller = kmalloc(sizeof(*controller));
    if (controller == NULL) {
        ERROR("CreateInterruptController: allocation failed for controller %u", id);
        return OS_EOOM;
    }
    memset(controller, 0, sizeof(*controller));
    controller->Id = id;
    controller->Type = SystemInterruptControllerLine;
    controller->Line.InterruptLineBase = interruptLineBase;
    controller->Line.NumberOfInterruptLines = numberOfInterrupts;
    controller->Line.MemoryAddress = baseAddress;

    SpinlockAcquireIrq(&g_controllerLock);
    __AppendInterruptController(controller);
    SpinlockReleaseIrq(&g_controllerLock);
    return OS_EOK;
}

SystemInterruptController_t*
GetNextInterruptController(
    _In_ SystemInterruptController_t* previous)
{
    SystemInterruptController_t* controller;

    SpinlockAcquireIrq(&g_controllerLock);
    controller = previous == NULL ? GetMachine()->InterruptController : previous->Link;
    SpinlockReleaseIrq(&g_controllerLock);
    return controller;
}

oserr_t
CreateInterruptOverrides(
    _In_ int NumberOfInterruptOverrides)
{
    int i;

    TRACE("CreateInterruptOverrides(count=%d)", NumberOfInterruptOverrides);
    assert(GetMachine()->Overrides == NULL);
    assert(NumberOfInterruptOverrides > 0);

    // Allocate the number of interrupts
    GetMachine()->NumberOfOverrides = NumberOfInterruptOverrides;
    GetMachine()->Overrides         = (SystemInterruptOverride_t*)kmalloc(
        sizeof(SystemInterruptOverride_t) * NumberOfInterruptOverrides);
    if (GetMachine()->Overrides == NULL) {
        ERROR("CreateInterruptOverrides: allocation failed for %d entries", NumberOfInterruptOverrides);
        return OS_EOOM;
    }

    // Set them unused
    for (i = 0; i < NumberOfInterruptOverrides; i++) {
        GetMachine()->Overrides[i].SourceLine = -1;
    }
    return OS_EOK;
}

oserr_t
RegisterInterruptOverride(
    _In_ int          SourceInterruptLine,
    _In_ int          DestinationInterruptLine,
    _In_ unsigned int InterruptFlags)
{
#ifdef __OSCONFIG_ACPI_SUPPORT
    int i;

    TRACE("RegisterInterruptOverride(source=%d, destination=%d, flags=0x%x)",
          SourceInterruptLine, DestinationInterruptLine, InterruptFlags);
    assert(GetMachine()->NumberOfOverrides > 0);

    // Find an unused entry and store it there
    for (i = 0; i < GetMachine()->NumberOfOverrides; i++) {
        if (GetMachine()->Overrides[i].SourceLine == -1) {
            GetMachine()->Overrides[i].SourceLine       = SourceInterruptLine;
            GetMachine()->Overrides[i].DestinationLine  = DestinationInterruptLine;
            GetMachine()->Overrides[i].OverrideFlags    = ConvertAcpiFlagsToConformFlags(InterruptFlags, DestinationInterruptLine);
            return OS_EOK;
        }
    }
    ERROR("RegisterInterruptOverride: no free entry for source %d, destination %d",
          SourceInterruptLine, DestinationInterruptLine);
    return OS_EUNKNOWN;
#else
    TRACE("RegisterInterruptOverride: ACPI overrides are not supported");
    (void)SourceInterruptLine;
    (void)DestinationInterruptLine;
    (void)InterruptFlags;
    return OS_ENOTSUPPORTED;
#endif
}

int
GetPinOffsetByLine(
    _In_ int interruptLine)
{
    SystemInterruptController_t* controller;

    controller = GetInterruptControllerByLine(interruptLine);
    if (controller == NULL) {
        return INTERRUPT_NONE;
    }
    return interruptLine - controller->Line.InterruptLineBase;
}

SystemInterruptController_t*
GetInterruptControllerByLine(
    _In_ int interruptLine)
{
    SystemInterruptController_t* controller;

    if (interruptLine < 0) {
        return NULL;
    }

    SpinlockAcquireIrq(&g_controllerLock);
    for (controller = GetMachine()->InterruptController; controller != NULL; controller = controller->Link) {

        // Only line controllers can own a system interrupt line.
        if (controller->Type != SystemInterruptControllerLine) {
            continue;
        }

        // Subtract only after the lower-bound check to avoid overflowing the
        // exclusive upper bound when the last supported line is INT_MAX.
        if (interruptLine >= controller->Line.InterruptLineBase &&
            (interruptLine - controller->Line.InterruptLineBase) < controller->Line.NumberOfInterruptLines) {
            break;
        }
    }
    SpinlockReleaseIrq(&g_controllerLock);
    return controller;
}

static int
__ValidControllerQuery(
    _In_ const InterruptMessageControllerIdentity_t* query)
{
    if (query == NULL || query->Version != 1 || query->Reserved != 0) {
        return 0;
    }

    if (query->Kind == InterruptMessageControllerKindPlatform) {
        return query->FirmwareNode == 0;
    }
    
    return query->Kind == InterruptMessageControllerKindDeviceTree;
}

static SystemInterruptController_t*
__FindMessageController(
    _In_ const InterruptMessageControllerIdentity_t* identity)
{
    SystemInterruptController_t* controller;

    for (controller = GetMachine()->InterruptController; controller != NULL; controller = controller->Link) {
        if (controller->Type != SystemInterruptControllerMessage) {
            continue;
        }
        if (controller->Message.Source.Kind == identity->Kind &&
            controller->Message.Source.FirmwareNode == identity->FirmwareNode) {
            return controller;
        }
    }
    return NULL;
}

static oserr_t
__ValidateInterruptMessageBackend(
    _In_ const struct InterruptMessageBackend* backend)
{
    if (backend == NULL) {
        WARNING("__ValidateInterruptMessageBackend: missing backend");
        return OS_EINVALPARAMS;
    }

    if (backend->Operations.Capacity == NULL || backend->Operations.AllocateOrdinal == NULL) {
        WARNING("__ValidateInterruptMessageBackend: missing Capacity or AllocateOrdinal callback");
        return OS_EINVALPARAMS;
    }

    if (backend->Operations.SetEnabled == NULL || backend->Operations.Release == NULL) {
        WARNING("__ValidateInterruptMessageBackend: missing SetEnabled or Release callback");
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

oserr_t
CreateInterruptMessageController(
    _In_  const InterruptMessageControllerIdentity_t* identity,
    _In_  const struct InterruptMessageBackend*       backend,
    _Out_ uuid_t*                                     idOut)
{
    SystemInterruptController_t* controller;
    oserr_t                      status = OS_EOK;

    TRACE("CreateInterruptMessageController(identity=%p, backend=%p)", identity, backend);
    if (InterruptGetActiveStatus() || idOut == NULL) {
        WARNING("CreateInterruptMessageController: interrupt context or missing output");
        return OS_EINVALPARAMS;
    }
    
    status = __ValidateInterruptMessageBackend(backend);
    if (status != OS_EOK) {
        return status;
    }

    if (!__ValidControllerQuery(identity)) {
        WARNING("CreateInterruptMessageController: invalid identity");
        return OS_EINVALPARAMS;
    }
    
    controller = kmalloc(sizeof(SystemInterruptController_t));
    if (controller == NULL) {
        ERROR("CreateInterruptMessageController: allocation failed for kind %u, node %u",
              identity->Kind, identity->FirmwareNode);
        return OS_EOOM;
    }

    memset(controller, 0, sizeof(SystemInterruptController_t));
    controller->Type = SystemInterruptControllerMessage;
    controller->Message.Source = *identity;
    controller->Message.Backend = *backend;

    SpinlockAcquireIrq(&g_controllerLock);
    if (__FindMessageController(identity) != NULL) {
        status = OS_EEXISTS;
    } else if (g_messageControllerIdGen == UINT32_MAX) {
        status = OS_EOVERFLOW;
    } else {
        controller->Id = ++g_messageControllerIdGen;
        __AppendInterruptController(controller);
        *idOut = controller->Id;
    }
    SpinlockReleaseIrq(&g_controllerLock);
    
    if (status != OS_EOK) {
        if (status == OS_EEXISTS) {
            WARNING("CreateInterruptMessageController: duplicate kind %u, node %u",
                    identity->Kind, identity->FirmwareNode);
        } else {
            ERROR("CreateInterruptMessageController: controller IDs exhausted");
        }
        kfree(controller);
        return status;
    }
    TRACE("CreateInterruptMessageController: controller %u, kind %u, node %u",
          controller->Id, identity->Kind, identity->FirmwareNode);
    return OS_EOK;
}

oserr_t
InterruptMessageControllerResolve(
    _In_  const InterruptMessageControllerIdentity_t* query,
    _Out_ uuid_t*                                     idOut)
{
    SystemInterruptController_t* controller;

    TRACE("InterruptMessageControllerResolve(query=%p)", query);
    if (!__ValidControllerQuery(query) || idOut == NULL) {
        WARNING("InterruptMessageControllerResolve: invalid query or missing output");
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_controllerLock);
    controller = __FindMessageController(query);
    if (controller != NULL) {
        *idOut = controller->Id;
    }
    SpinlockReleaseIrq(&g_controllerLock);
    if (controller == NULL) {
        TRACE("InterruptMessageControllerResolve: no controller for kind %u, node %u",
              query->Kind, query->FirmwareNode);
        return OS_ENOTSUPPORTED;
    }
    TRACE("InterruptMessageControllerResolve: controller %u", controller->Id);
    return OS_EOK;
}

oserr_t
InterruptMessageControllerLookup(
    _In_  uuid_t                        controllerId,
    _In_  int                           kernelHandlers,
    _Out_ SystemInterruptController_t** controllerOut)
{
    const InterruptMessageControllerIdentity_t platform = {
        .Version = 1,
        .Kind = InterruptMessageControllerKindPlatform
    };
    SystemInterruptController_t* controller;
    oserr_t                      status;

    TRACE("InterruptMessageControllerLookup(id=%u, kernel=%d)", controllerId, kernelHandlers);
    if (controllerId == UUID_INVALID && kernelHandlers) {
        status = InterruptMessageControllerResolve(&platform, &controllerId);
        if (status != OS_EOK) {
            return status;
        }
    }
    
    if (controllerId == UUID_INVALID) {
        TRACE("InterruptMessageControllerLookup: no message controller selected");
        return OS_ENOTSUPPORTED;
    }

    SpinlockAcquireIrq(&g_controllerLock);
    for (controller = GetMachine()->InterruptController; controller != NULL; controller = controller->Link) {
        if (controller->Type == SystemInterruptControllerMessage && controller->Id == controllerId) {
            *controllerOut = controller;
            break;
        }
    }
    SpinlockReleaseIrq(&g_controllerLock);
    if (controller == NULL) {
        TRACE("InterruptMessageControllerLookup: controller %u not found", controllerId);
        return OS_ENOENT;
    }
    return OS_EOK;
}

uint32_t
InterruptMessageControllerCapacity(
    _In_ SystemInterruptController_t* controller)
{
    const struct InterruptMessageBackend* backend = &controller->Message.Backend;

    return backend->Operations.Capacity(backend->Context);
}

oserr_t
InterruptControllerResolve(
    _InOut_ InterruptDescriptor_t*         descriptor,
    _In_    unsigned int                   flags,
    _Out_   InterruptControllerResource_t* resource)
{
    oserr_t status;

    TRACE("InterruptControllerResolve(line=%d, pin=%d, flags=0x%x)",
          descriptor->Line, descriptor->Pin, flags);
    memset(resource, 0, sizeof(*resource));
    status = InterruptResolve(descriptor, flags, &resource->TableIndex);
    if (status != OS_EOK) {
        ERROR("InterruptControllerResolve: InterruptResolve line %d, flags 0x%x failed: %u",
              descriptor->Line, flags, status);
        return status;
    }

    resource->Flags = flags;
    resource->Line = descriptor->Line;
    resource->Source = descriptor->Line;
    resource->Pin = descriptor->Pin;
    resource->AcpiConform = descriptor->AcpiConform;
    if (flags & INTERRUPT_SOFT) {
        resource->Controller = &g_localController;
    } else {
        resource->Controller = GetInterruptControllerByLine(descriptor->Line);
        if (resource->Controller == NULL) {
            resource->Controller = &g_platformLineController;
        }
    }
    TRACE("InterruptControllerResolve: table index %u, controller type %u",
          resource->TableIndex, resource->Controller->Type);
    return OS_EOK;
}

oserr_t
InterruptControllerAllocateMessage(
    _In_  SystemInterruptController_t*   controller,
    _In_  uint32_t                       ordinal,
    _In_  unsigned int                   flags,
    _Out_ InterruptControllerResource_t* resource)
{
    const struct InterruptMessageBackend* backend = &controller->Message.Backend;
    uint32_t                              capacity;
    oserr_t                               status;

    TRACE("InterruptControllerAllocateMessage(controller=%u, ordinal=%u, flags=0x%x)",
          controller->Id, ordinal, flags);
    capacity = backend->Operations.Capacity(backend->Context);
    if (ordinal >= capacity) {
        WARNING("InterruptControllerAllocateMessage: ordinal %u exceeds controller %u capacity %u",
                ordinal, controller->Id, capacity);
        return OS_EINVALPARAMS;
    }
    memset(resource, 0, sizeof(*resource));
    resource->Controller = controller;
    resource->Flags = flags;
    resource->Source = INTERRUPT_NONE;
    resource->Pin = INTERRUPT_NONE;
    status = backend->Operations.AllocateOrdinal(
        backend->Context,
        ordinal,
        resource
    );
    if (status != OS_EOK) {
        if (status == OS_EBUSY || status == OS_ENOTSUPPORTED) {
            TRACE("InterruptControllerAllocateMessage: controller %u ordinal %u unavailable: %u",
                  controller->Id, ordinal, status);
        } else {
            ERROR("InterruptControllerAllocateMessage: AllocateOrdinal controller %u ordinal %u failed: %u",
                  controller->Id, ordinal, status);
        }
        return status;
    }
    TRACE("InterruptControllerAllocateMessage: controller %u, table index %u, message 0x%llx/0x%x",
          controller->Id, resource->TableIndex,
          (unsigned long long)resource->Message.Address, resource->Message.Data);
    return OS_EOK;
}

oserr_t
InterruptControllerSetEnabled(
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  enable)
{
    const struct InterruptMessageBackend* backend;
    oserr_t                               status;

    TRACE("InterruptControllerSetEnabled(type=%u, controller=%u, index=%u, enable=%d)",
          resource->Controller->Type, resource->Controller->Id, resource->TableIndex, enable);
    if (resource->Controller->Type != SystemInterruptControllerMessage) {
        status = InterruptConfigure(resource, enable);
        if (status != OS_EOK) {
            ERROR("InterruptControllerSetEnabled: InterruptConfigure line %d, enable %d failed: %u",
                  resource->Line, enable, status);
        }
        return status;
    }
    backend = &resource->Controller->Message.Backend;
    status = backend->Operations.SetEnabled(backend->Context, resource, enable);
    if (status != OS_EOK) {
        ERROR("InterruptControllerSetEnabled: SetEnabled controller %u, index %u, enable %d failed: %u",
              resource->Controller->Id, resource->TableIndex, enable, status);
    }
    return status;
}

oserr_t
InterruptControllerRelease(
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  retire)
{
    const struct InterruptMessageBackend* backend;
    oserr_t                               status;

    TRACE("InterruptControllerRelease(type=%u, controller=%u, index=%u, retire=%d)",
          resource->Controller->Type, resource->Controller->Id, resource->TableIndex, retire);
    if (resource->Controller->Type != SystemInterruptControllerMessage) {
        return OS_EOK;
    }
    backend = &resource->Controller->Message.Backend;
    status = backend->Operations.Release(backend->Context, resource, retire);
    if (status != OS_EOK) {
        ERROR("InterruptControllerRelease: Release controller %u, index %u, retire %d failed: %u",
              resource->Controller->Id, resource->TableIndex, retire, status);
    }
    return status;
}
