/**
 * MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that is generic and can be shared/used by all systems
 */

#define __MODULE "INIF"
//#define __TRACE

#include <arch/interrupts.h>
#include <arch/platform.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <ddk/interrupt.h>
#include <deviceio.h>
#include <debug.h>
#include <heap.h>
#include <handle.h>
#include <memoryspace.h>
#include <spinlock.h>
#include <interrupts.h>
#include <threading.h>
#include <userevent.h>
#include <string.h>

static oserr_t
InterruptReleaseResources(
        _In_ SystemInterrupt_t* Interrupt);

typedef struct InterruptTableEntry {
    _Atomic(SystemInterrupt_t*) Descriptor;
    int                         Penalty;
    int                         Sharable;
    int                         MsiReserved;
    int                         MsiQuarantined;
    uint32_t                    MsiControllerId;
    uint32_t                    MsiHwIrq;
} InterruptTableEntry_t;

typedef struct __InterruptMsiQuiesceEntry {
    struct __InterruptMsiQuiesceEntry* Next;
    DeviceInterruptQuiesceRequest_t   Request;
    uuid_t                             Owner;
    uint32_t                           Count;
    InterruptMsiRoute_t                Routes[INTERRUPT_MAXVECTORS];
} __InterruptMsiQuiesceEntry_t;

typedef struct __InterruptMsiController {
    uint32_t Id;
    DeviceMsiControllerDescription_t Description;
    struct {
        uint32_t Segment;
        uint8_t  BusStart;
        uint8_t  BusEnd;
    } DeviceRanges[8];
    uint32_t DeviceRangeCount;
    uint64_t AllocatedMessages;
} __InterruptMsiController_t;

static InterruptTableEntry_t g_interruptTable[MAX_SUPPORTED_INTERRUPTS] = { { 0 } };
static Spinlock_t            g_interruptTableLock                       = OS_SPINLOCK_INIT;
static _Atomic(uuid_t)       g_nextInterruptId                          = 0;

/*
 * Interrupt dispatch uses a small RCU read-side critical section. Readers
 * only update this counter and never wait for a lock. Unregistered entries
 * are placed on the retired list and reclaimed after all readers have left.
 */
static _Atomic(unsigned int) g_interruptReaders = 0;
static SystemInterrupt_t*    g_retiredInterrupts;
static Spinlock_t            g_msiQuiesceLock   = OS_SPINLOCK_INIT;
static __InterruptMsiQuiesceEntry_t* g_msiQuiesceHead;
static uuid_t                 g_msiQuiesceOwner = UUID_INVALID;
static uuid_t                 g_msiQuiesceEvent = UUID_INVALID;
static _Atomic(uuid_t)        g_nextMsiQuiesceToken = 1;
static Spinlock_t             g_msiControllerLock = OS_SPINLOCK_INIT;
static __InterruptMsiController_t g_msiControllers[8];
static uint32_t                g_nextMsiControllerId = INTERRUPT_MSI_CONTROLLER_FIRST_PLATFORM;

static void
InterruptReadEnter(void)
{
    atomic_fetch_add(&g_interruptReaders, 1);
}

static void
InterruptReadExit(void)
{
    atomic_fetch_sub(&g_interruptReaders, 1);
}

static void
InterruptRetire(
        _In_ SystemInterrupt_t* Entry)
{
    Entry->RetiredLink = g_retiredInterrupts;
    g_retiredInterrupts = Entry;
}

// Must be called with g_interruptTableLock held.
static void
InterruptReleasePenaltyLocked(
        _In_ SystemInterrupt_t* Entry)
{
    InterruptTableEntry_t* tableEntry = &g_interruptTable[Entry->Index];

    if (!Entry->HasPenalty) {
        return;
    }
    Entry->HasPenalty = 0;
    if (Entry->QuarantineMsi) {
        tableEntry->MsiQuarantined = 1;
        return;
    }
    if (Entry->MsiRoute.Flags & INTERRUPT_MSI_ROUTE_PARENT_RESERVED) {
        tableEntry->MsiHwIrq = UINT32_MAX;
        return;
    }
    if (tableEntry->Penalty > 0) {
        tableEntry->Penalty--;
    }
    if (tableEntry->Penalty == 0) {
        tableEntry->Sharable = 0;
    }
    if (Entry->MsiRoute.ControllerId != 0) {
        tableEntry->MsiReserved = 0;
        tableEntry->MsiControllerId = 0;
        tableEntry->MsiHwIrq = 0;
    }
}

oserr_t
InterruptMsiReserveTableRoute(
    _In_ const InterruptMsiRoute_t* route)
{
    InterruptTableEntry_t* tableEntry;

    if (route == NULL || route->ControllerId == 0 ||
        route->Index >= MAX_SUPPORTED_INTERRUPTS) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    tableEntry = &g_interruptTable[route->Index];
    if ((route->Flags & INTERRUPT_MSI_ROUTE_PARENT_RESERVED) && route->HwIrq == UINT32_MAX) {
        if (tableEntry->MsiReserved || tableEntry->MsiQuarantined ||
            tableEntry->Penalty != 0 ||
            atomic_load(&tableEntry->Descriptor) != NULL) {
            SpinlockReleaseIrq(&g_interruptTableLock);
            return OS_EBUSY;
        }
        tableEntry->MsiReserved = 1;
        tableEntry->MsiControllerId = route->ControllerId;
        tableEntry->MsiHwIrq = UINT32_MAX;
        tableEntry->Penalty = 1;
        tableEntry->Sharable = 0;
    } else if ((route->Flags & INTERRUPT_MSI_ROUTE_PARENT_RESERVED) &&
               tableEntry->MsiReserved && !tableEntry->MsiQuarantined &&
               tableEntry->MsiControllerId == route->ControllerId &&
               tableEntry->MsiHwIrq == UINT32_MAX &&
               atomic_load(&tableEntry->Descriptor) == NULL) {
        tableEntry->MsiHwIrq = route->HwIrq;
    } else if (!tableEntry->MsiReserved && !tableEntry->MsiQuarantined &&
               tableEntry->Penalty == 0 &&
               atomic_load(&tableEntry->Descriptor) == NULL) {
        tableEntry->MsiReserved = 1;
        tableEntry->MsiControllerId = route->ControllerId;
        tableEntry->MsiHwIrq = route->HwIrq;
        tableEntry->Penalty = 1;
        tableEntry->Sharable = 0;
    } else {
        SpinlockReleaseIrq(&g_interruptTableLock);
        return OS_EBUSY;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    return OS_EOK;
}

oserr_t
InterruptMsiReleaseTableRoute(
    _In_ const InterruptMsiRoute_t* route)
{
    InterruptTableEntry_t* tableEntry;

    if (route == NULL || route->Index >= MAX_SUPPORTED_INTERRUPTS) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    tableEntry = &g_interruptTable[route->Index];
    if (!tableEntry->MsiReserved || tableEntry->MsiQuarantined ||
        tableEntry->MsiControllerId != route->ControllerId ||
        atomic_load(&tableEntry->Descriptor) != NULL ||
        (route->HwIrq != UINT32_MAX && tableEntry->MsiHwIrq != route->HwIrq)) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        return OS_EBUSY;
    }
    if ((route->Flags & INTERRUPT_MSI_ROUTE_PARENT_RESERVED) && route->HwIrq != UINT32_MAX) {
        tableEntry->MsiHwIrq = UINT32_MAX;
    } else {
        tableEntry->MsiReserved = 0;
        tableEntry->MsiControllerId = 0;
        tableEntry->MsiHwIrq = 0;
        if (tableEntry->Penalty > 0) {
            tableEntry->Penalty--;
        }
        tableEntry->Sharable = 0;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    return OS_EOK;
}

uuid_t
InterruptMsiControllerRegister(
    _In_ const DeviceMsiControllerDescription_t* description)
{
    __InterruptMsiController_t* controller = NULL;
    uint32_t                    controllerId;
    oserr_t                     oserr;

    if (description == NULL || description->Type != DEVICE_MSI_CONTROLLER_MIP ||
        description->ProviderId == 0 ||
        description->BusStart > description->BusEnd ||
        description->MessageCount == 0 || description->MessageCount > 64 ||
        description->MessageOffset >= 64 ||
        description->MessageCount > 64 - description->MessageOffset ||
        description->ParentLine < 32 || description->ParentLine >= (int)MAX_SUPPORTED_INTERRUPTS ||
        description->MessageCount - 1 + description->MessageOffset >
                (uint32_t)((int)MAX_SUPPORTED_INTERRUPTS - 1 - description->ParentLine) ||
        description->DoorbellLength == 0 ||
        description->DoorbellLength - 1 > UINT64_MAX - description->DoorbellAddress) {
        return UUID_INVALID;
    }

    SpinlockAcquireIrq(&g_msiControllerLock);
    for (uint32_t i = 0; i < sizeof(g_msiControllers) / sizeof(g_msiControllers[0]); i++) {
        __InterruptMsiController_t* current = &g_msiControllers[i];
        if (current->Id == 0) {
            if (controller == NULL) {
                controller = current;
            }
            continue;
        }
        if (current->Description.ProviderId == description->ProviderId) {
            if (current->Description.Type != description->Type ||
                current->Description.ParentLine != description->ParentLine ||
                current->Description.MessageOffset != description->MessageOffset ||
                current->Description.MessageCount != description->MessageCount ||
                current->Description.DoorbellAddress != description->DoorbellAddress ||
                current->Description.DoorbellLength != description->DoorbellLength) {
                SpinlockReleaseIrq(&g_msiControllerLock);
                return UUID_INVALID;
            }
            for (uint32_t range = 0; range < current->DeviceRangeCount; range++) {
                if (current->DeviceRanges[range].Segment != description->Segment) {
                    continue;
                }
                if (current->DeviceRanges[range].BusStart == description->BusStart &&
                    current->DeviceRanges[range].BusEnd == description->BusEnd) {
                    controllerId = current->Id;
                    SpinlockReleaseIrq(&g_msiControllerLock);
                    return controllerId;
                }
                if (description->BusStart <= current->DeviceRanges[range].BusEnd &&
                    current->DeviceRanges[range].BusStart <= description->BusEnd) {
                    SpinlockReleaseIrq(&g_msiControllerLock);
                    return UUID_INVALID;
                }
            }
            if (current->DeviceRangeCount >= 8) {
                SpinlockReleaseIrq(&g_msiControllerLock);
                return UUID_INVALID;
            }
            current->DeviceRanges[current->DeviceRangeCount].Segment = description->Segment;
            current->DeviceRanges[current->DeviceRangeCount].BusStart = description->BusStart;
            current->DeviceRanges[current->DeviceRangeCount].BusEnd = description->BusEnd;
            current->DeviceRangeCount++;
            SpinlockReleaseIrq(&g_msiControllerLock);
            return current->Id;
        }
    }
    if (controller == NULL || g_nextMsiControllerId == 0) {
        SpinlockReleaseIrq(&g_msiControllerLock);
        return UUID_INVALID;
    }

    controllerId = g_nextMsiControllerId++;
    for (uint32_t i = 0; i < description->MessageCount; i++) {
        InterruptMsiRoute_t route;
        route.ControllerId = controllerId;
        route.HwIrq = UINT32_MAX;
        route.Index = (uint16_t)(description->ParentLine + description->MessageOffset + i);
        route.ParentLine = route.Index;
        route.Flags = INTERRUPT_MSI_ROUTE_PARENT_RESERVED;
        oserr = InterruptMsiReserveTableRoute(&route);
        if (oserr != OS_EOK) {
            while (i > 0) {
                i--;
                route.Index = (uint16_t)(description->ParentLine + description->MessageOffset + i);
                route.ParentLine = route.Index;
                (void)InterruptMsiReleaseTableRoute(&route);
            }
            SpinlockReleaseIrq(&g_msiControllerLock);
            return UUID_INVALID;
        }
    }

    controller->Id = controllerId;
    controller->Description = *description;
    controller->DeviceRanges[0].Segment = description->Segment;
    controller->DeviceRanges[0].BusStart = description->BusStart;
    controller->DeviceRanges[0].BusEnd = description->BusEnd;
    controller->DeviceRangeCount = 1;
    controller->AllocatedMessages = 0;
    SpinlockReleaseIrq(&g_msiControllerLock);
    return controllerId;
}

oserr_t
InterruptMsiControllerAllocate(
    _InOut_ DeviceInterrupt_t* deviceInterrupt)
{
    __InterruptMsiController_t* controller = NULL;
    InterruptMsiRoute_t        route;
    oserr_t                    oserr;

    if (deviceInterrupt == NULL || !deviceInterrupt->IsPci) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_msiControllerLock);
    for (uint32_t i = 0; i < sizeof(g_msiControllers) / sizeof(g_msiControllers[0]); i++) {
        __InterruptMsiController_t* current = &g_msiControllers[i];
        if (current->Id == 0) {
            continue;
        }
        for (uint32_t range = 0; range < current->DeviceRangeCount; range++) {
            if (current->DeviceRanges[range].Segment == deviceInterrupt->Segment &&
                deviceInterrupt->Bus >= current->DeviceRanges[range].BusStart &&
                deviceInterrupt->Bus <= current->DeviceRanges[range].BusEnd) {
                controller = current;
                break;
            }
        }
        if (controller != NULL) {
            break;
        }
    }
    if (controller == NULL) {
        SpinlockReleaseIrq(&g_msiControllerLock);
        return OS_ENOENT;
    }

    for (uint32_t slot = 0; slot < controller->Description.MessageCount; slot++) {
        uint64_t bit = 1ULL << slot;
        uint32_t hwIrq;
        uint32_t parentLine;

        if (controller->AllocatedMessages & bit) {
            continue;
        }
        hwIrq = controller->Description.MessageOffset + slot;
        parentLine = controller->Description.ParentLine + hwIrq;
        route.ControllerId = controller->Id;
        route.HwIrq = hwIrq;
        route.Index = (uint16_t)parentLine;
        route.ParentLine = (int)parentLine;
        route.Flags = INTERRUPT_MSI_ROUTE_PARENT_RESERVED;
        oserr = InterruptMsiReserveTableRoute(&route);
        if (oserr != OS_EOK) {
            continue;
        }

        controller->AllocatedMessages |= bit;
        deviceInterrupt->MsiControllerId = route.ControllerId;
        deviceInterrupt->MsiHwIrq = route.HwIrq;
        deviceInterrupt->MsiIndex = route.Index;
        deviceInterrupt->MsiParentLine = route.ParentLine;
        deviceInterrupt->MsiRouteFlags = route.Flags;
        deviceInterrupt->MsiAddress = controller->Description.DoorbellAddress;
        deviceInterrupt->MsiValue = hwIrq;
        SpinlockReleaseIrq(&g_msiControllerLock);
        return OS_EOK;
    }

    SpinlockReleaseIrq(&g_msiControllerLock);
    return OS_EOOM;
}

void
InterruptMsiControllerRelease(
    _In_ const InterruptMsiRoute_t* route)
{
    if (route == NULL) {
        return;
    }
    SpinlockAcquireIrq(&g_msiControllerLock);
    for (uint32_t i = 0; i < sizeof(g_msiControllers) / sizeof(g_msiControllers[0]); i++) {
        __InterruptMsiController_t* controller = &g_msiControllers[i];
        uint32_t                    slot;
        uint64_t                    bit;

        if (controller->Id != route->ControllerId ||
            !(route->Flags & INTERRUPT_MSI_ROUTE_PARENT_RESERVED) ||
            route->HwIrq < controller->Description.MessageOffset) {
            continue;
        }
        slot = route->HwIrq - controller->Description.MessageOffset;
        if (slot >= controller->Description.MessageCount ||
            route->ParentLine != (int)(controller->Description.ParentLine + route->HwIrq) ||
            route->Index != (uint16_t)route->ParentLine) {
            break;
        }
        bit = 1ULL << slot;
        controller->AllocatedMessages &= ~bit;
        break;
    }
    SpinlockReleaseIrq(&g_msiControllerLock);
}

oserr_t
InterruptMsiCommitRoutes(
    _In_ const uuid_t* sources,
    _In_ uint32_t      count)
{
    SystemInterrupt_t* entry;

    if (sources == NULL || count == 0 || count > INTERRUPT_MAXVECTORS) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    for (uint32_t i = 0; i < count; i++) {
        uint16_t index = LOWORD(sources[i]);
        if (index >= MAX_SUPPORTED_INTERRUPTS) {
            SpinlockReleaseIrq(&g_interruptTableLock);
            return OS_EINVALPARAMS;
        }
        entry = atomic_load(&g_interruptTable[index].Descriptor);
        while (entry != NULL && entry->Id != sources[i]) {
            entry = atomic_load(&entry->Link);
        }
        if (entry == NULL || entry->MsiRoute.ControllerId == 0 ||
            !g_interruptTable[index].MsiReserved || entry->QuarantineMsi) {
            SpinlockReleaseIrq(&g_interruptTableLock);
            return OS_ENOENT;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        uint16_t index = LOWORD(sources[i]);
        entry = atomic_load(&g_interruptTable[index].Descriptor);
        while (entry != NULL && entry->Id != sources[i]) {
            entry = atomic_load(&entry->Link);
        }
        entry->QuarantineMsi = 1;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    return OS_EOK;
}

static void
InterruptReclaimRetired(void)
{
    SystemInterrupt_t* retired;
    SystemInterrupt_t* next;
    SystemInterrupt_t* reclaimable = NULL;

    // Never reclaim an entry while an interrupt handler can still reference it.
    if (atomic_load(&g_interruptReaders) != 0) {
        return;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    if (atomic_load(&g_interruptReaders) != 0) {
        SpinlockReleaseIrq(&g_interruptTableLock);
        return;
    }

    // Keep descriptors with an outstanding InterruptGet() reference on the
    // retired list. They are no longer dispatchable, but their storage and
    // user mapping must remain valid until the reference is released.
    retired = g_retiredInterrupts;
    g_retiredInterrupts = NULL;
    while (retired != NULL) {
        next = retired->RetiredLink;
        // The grace period has elapsed, so the table index may be handed out again.
        InterruptReleasePenaltyLocked(retired);
        if (atomic_load(&retired->References) == 0) {
            retired->RetiredLink = reclaimable;
            reclaimable = retired;
        } else {
            retired->RetiredLink = g_retiredInterrupts;
            g_retiredInterrupts = retired;
        }
        retired = next;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);

    while (reclaimable != NULL) {
        next = reclaimable->RetiredLink;
        if (reclaimable->Owner != UUID_INVALID) {
            if (InterruptReleaseResources(reclaimable) != OS_EOK) {
                ERROR(" > failed to cleanup interrupt resources");
            }
        }
        if (reclaimable->MsiRoute.ControllerId != 0 && !reclaimable->QuarantineMsi) {
            PlatformMsiRelease(&reclaimable->MsiRoute);
        }
        kfree(reclaimable);
        reclaimable = next;
    }
}

oserr_t
InterruptIncreasePenalty(
    _In_ int Source)
{
    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        return INTERRUPT_NONE;
    }
    SpinlockAcquireIrq(&g_interruptTableLock);
    g_interruptTable[Source].Penalty++;
    SpinlockReleaseIrq(&g_interruptTableLock);
    return OS_EOK;
}

oserr_t
InterruptDecreasePenalty(
    _In_ int Source)
{
    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        return INTERRUPT_NONE;
    }
    SpinlockAcquireIrq(&g_interruptTableLock);
    g_interruptTable[Source].Penalty--;
    SpinlockReleaseIrq(&g_interruptTableLock);
    return OS_EOK;
}

int
InterruptGetPenalty(
    _In_ int Source)
{
    int penalty;

    // Sanitize the requested source bounds
    if (Source < 0 || Source >= MAX_SUPPORTED_INTERRUPTS) {
        return INTERRUPT_NONE;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    penalty = g_interruptTable[Source].Penalty;
    if (g_interruptTable[Source].Sharable == 0 && penalty > 0) {
        penalty = INTERRUPT_NONE;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    return penalty;
}

int
InterruptGetLeastLoaded(
    _In_ int interruptVectors[],
    _In_ int count)
{
    int SelectedPenality = INTERRUPT_NONE;
    int SelectedIrq      = INTERRUPT_NONE;
    int i;

    TRACE("InterruptGetLeastLoaded(Count %" PRIiIN ")", count);

    // Iterate all the available irqs
    // that the device-supports
    for (i = 0; i < count; i++) {
        if (interruptVectors[i] == INTERRUPT_NONE) {
            break;
        }

        // Calculate count
        int Penalty = InterruptGetPenalty(interruptVectors[i]);

        // Sanitize status, if -1 then its not usable
        if (Penalty == INTERRUPT_NONE) {
            continue;
        }

        // Store the lowest penalty
        if (SelectedIrq == INTERRUPT_NONE || Penalty < SelectedPenality) {
            SelectedIrq = interruptVectors[i];
            SelectedPenality = Penalty;
        }
    }
    return SelectedIrq;
}

/* InterruptCleanupIoResources
 * Releases all kernel copies of the io-resources. */
static oserr_t
InterruptCleanupIoResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* Resources = &Interrupt->KernelResources;
    oserr_t                   Status    = OS_EOK;

    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Resources->IoResources[i] != NULL) {
            Status = ReleaseKernelSystemDeviceIo(Resources->IoResources[i]);
            if (Status != OS_EOK) {
                ERROR(" > failed to cleanup system copy of io-resource");
                break;
            }
            Resources->IoResources[i] = NULL;
        }
    }
    return Status;
}

/* InterruptResolveIoResources
 * Retrieves kernel copies of all requested io-resources, and remaps them into
 * kernel space to allow the handler to access them. */
static oserr_t
InterruptResolveIoResources(
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ SystemInterrupt_t* systemInterrupt)
{
    InterruptResourceTable_t* Source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* Destination = &systemInterrupt->KernelResources;
    oserr_t                   Status      = OS_EOK;

    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Source->IoResources[i] != NULL) {
            Status = CreateKernelSystemDeviceIo(Source->IoResources[i], &Destination->IoResources[i]);
            if (Status != OS_EOK) {
                ERROR(" > failed to create system copy of io-resource");
                break;
            }
        }
    }
    
    if (Status != OS_EOK) {
        (void)InterruptCleanupIoResources(systemInterrupt);
        return OS_EUNKNOWN;
    }
    return Status;
}

/* InterruptCleanupMemoryResources
 * Releases all memory copies of the interrupt memory resources. */
static oserr_t
InterruptCleanupMemoryResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* Resources = &Interrupt->KernelResources;
    oserr_t                   Status    = OS_EOK;

    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (Resources->MemoryResources[i].Address != 0) {
            uintptr_t Offset    = Resources->MemoryResources[i].Address % GetMemorySpacePageSize();
            size_t Length       = Resources->MemoryResources[i].Length + Offset;

            Status = MemorySpaceUnmap(GetCurrentMemorySpace(),
                Resources->MemoryResources[i].Address, Length);
            if (Status != OS_EOK) {
                ERROR(" > failed to remove interrupt resource mapping");
                break;
            }
            Resources->MemoryResources[i].Address = 0;
        }
    }
    return Status;
}

/* InterruptResolveMemoryResources
 * Retrieves kernel copies of all requested memory-resources, and remaps them into
 * kernel space to allow the handler to access them. */
static oserr_t
InterruptResolveMemoryResources(
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ SystemInterrupt_t* systemInterrupt)
{
    InterruptResourceTable_t* source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* destination = &systemInterrupt->KernelResources;
    oserr_t                   oserr = OS_EOK;
    uintptr_t                 updatedMapping;

    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (source->MemoryResources[i].Address != 0) {
            uintptr_t    offset         = source->MemoryResources[i].Address % GetMemorySpacePageSize();
            size_t       length         = source->MemoryResources[i].Length + offset;
            unsigned int pageFlags      = MAPPING_COMMIT | MAPPING_PERSISTENT;
            unsigned int placementFlags = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED;
            if (source->MemoryResources[i].Flags & INTERRUPT_RESOURCE_DISABLE_CACHE) {
                pageFlags |= MAPPING_NOCACHE;
            }

            oserr = MemorySpaceCloneMapping(
                    GetCurrentMemorySpace(),
                    GetCurrentMemorySpace(),
                    source->MemoryResources[i].Address,
                    &updatedMapping,
                    length,
                    pageFlags,
                    placementFlags
            );
            if (oserr != OS_EOK) {
                ERROR(" > failed to clone interrupt resource mapping");
                break;
            }
            TRACE(" > remapped resource to 0x%" PRIxIN " from 0x%" PRIxIN "", updatedMapping + offset, source->MemoryResources[i]);
            destination->MemoryResources[i].Address = updatedMapping + offset;
            destination->MemoryResources[i].Length  = source->MemoryResources[i].Length;
            destination->MemoryResources[i].Flags   = source->MemoryResources[i].Flags;
        }
    }

    if (oserr != OS_EOK) {
        (void)InterruptCleanupMemoryResources(systemInterrupt);
        return OS_EUNKNOWN;
    }
    return oserr;
}

/**
 * Maps the neccessary fast-interrupt resources into kernel space
 * and allowing the interrupt handler to access the requested memory spaces.
 * @param deviceInterrupt
 * @param systemInterrupt
 * @return
 */
static oserr_t
InterruptResolveResources(
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ SystemInterrupt_t* systemInterrupt)
{
    InterruptResourceTable_t* Source      = &deviceInterrupt->ResourceTable;
    InterruptResourceTable_t* Destination = &systemInterrupt->KernelResources;
    unsigned int              PlacementFlags;
    unsigned int              PageFlags;
    oserr_t                   Status;
    uintptr_t                 Virtual;
    uintptr_t                 Offset;
    size_t                    Length;

    TRACE("InterruptResolveResources()");

    // Calculate metrics we need to create the mappings
    Offset         = ((uintptr_t)Source->Handler) % GetMemorySpacePageSize();
    Length         = GetMemorySpacePageSize() + Offset;
    PageFlags      = MAPPING_COMMIT | MAPPING_EXECUTABLE | MAPPING_READONLY;
    PlacementFlags = MAPPING_VIRTUAL_GLOBAL;
    
    Status         = MemorySpaceCloneMapping(
        GetCurrentMemorySpace(),
        GetCurrentMemorySpace(),
        (vaddr_t) Source->Handler,
        &Virtual,
        Length,
        PageFlags,
        PlacementFlags
    );
    if (Status != OS_EOK) {
        ERROR(" > failed to clone interrupt handler mapping");
        return Status;
    }
    Virtual += Offset;

    TRACE(" > remapped irq-handler to 0x%" PRIxIN " from 0x%" PRIxIN "", Virtual, (uintptr_t)Source->Handler);
    Destination->Handler = (InterruptHandler_t)Virtual;

    TRACE(" > remapping io-resources");

    Status = InterruptResolveIoResources(deviceInterrupt, systemInterrupt);
    if (Status != OS_EOK) {
        ERROR(" > failed to remap interrupt io resources");
        (void)InterruptReleaseResources(systemInterrupt);
        return Status;
    }

    TRACE(" > remapping memory-resources");
    Status = InterruptResolveMemoryResources(deviceInterrupt, systemInterrupt);
    if (Status != OS_EOK) {
        ERROR(" > failed to remap interrupt memory resources");
        (void)InterruptReleaseResources(systemInterrupt);
        return Status;
    }
    return Status;
}

/* InterruptReleaseResources
 * Releases previously allocated resources for the system interrupt. */
static oserr_t
InterruptReleaseResources(
    _In_ SystemInterrupt_t* Interrupt)
{
    InterruptResourceTable_t* resources = &Interrupt->KernelResources;
    oserr_t                   osStatus;
    uintptr_t                 Offset;
    size_t                    Length;

    // Sanitize a handler is present, if not, no resources are present
    if ((uintptr_t)resources->Handler == 0) {
        return OS_EOK;
    }

    // Unmap and release the fast-handler that we had mapped in.
    Offset      = ((uintptr_t)resources->Handler) % GetMemorySpacePageSize();
    Length      = GetMemorySpacePageSize() + Offset;
    osStatus      = MemorySpaceUnmap(GetCurrentMemorySpace(),
        (uintptr_t)resources->Handler, Length);
    if (osStatus != OS_EOK) {
        ERROR(" > failed to cleanup interrupt handler mapping");
        return osStatus;
    }

    osStatus = InterruptCleanupIoResources(Interrupt);
    if (osStatus != OS_EOK) {
        ERROR(" > failed to cleanup interrupt io resources");
        return osStatus;
    }

    osStatus = InterruptCleanupMemoryResources(Interrupt);
    if (osStatus != OS_EOK) {
        ERROR(" > failed to cleanup interrupt memory resources");
        return osStatus;
    }
    return osStatus;
}

uuid_t
InterruptRegister(
        _In_ DeviceInterrupt_t* deviceInterrupt,
        _In_ unsigned int       flags)
{
    SystemInterrupt_t* systemInterrupt;
    uuid_t             tableIndex;
    uuid_t             id;
    InterruptMsiRoute_t msiRoute = { 0 };
    int                 allocatedMsiRoute = 0;
    oserr_t             oserr;

    if (!deviceInterrupt) {
        return UUID_INVALID;
    }

    // MSI routes occupy a controller vector exclusively, regardless of the
    // sharing flags supplied by the caller.
    if (flags & INTERRUPT_MSI) {
        flags |= INTERRUPT_EXCLUSIVE;
    }

    // Reclaim entries retired by an earlier interrupt-context unregister.
    InterruptReclaimRetired();

    TRACE("InterruptRegister(Line %i Pin %i, Vector %i, Flags 0x%" PRIxIN ")",
        deviceInterrupt->Line, deviceInterrupt->Pin, deviceInterrupt->Vectors[0], flags);

    systemInterrupt = (SystemInterrupt_t*)kmalloc(sizeof(SystemInterrupt_t));
    if (!systemInterrupt) {
        return UUID_INVALID;
    }
    
    // TODO: change this to use handle system
    id = atomic_fetch_add(&g_nextInterruptId, 1);
    memset((void*)systemInterrupt, 0, sizeof(SystemInterrupt_t));
    atomic_init(&systemInterrupt->References, 0);

    systemInterrupt->Id           = (id << 16U);
    systemInterrupt->Owner        = UUID_INVALID;
    systemInterrupt->Thread       = ThreadCurrentHandle();
    systemInterrupt->Flags        = flags;
    systemInterrupt->Pin          = deviceInterrupt->Pin;
    systemInterrupt->AcpiConform  = deviceInterrupt->AcpiConform;

    // Get process id?
    if (!(flags & INTERRUPT_KERNEL)) {
        systemInterrupt->Owner = GetCurrentMemorySpaceHandle();
    }

    if (flags & INTERRUPT_MSI) {
        if (deviceInterrupt->MsiControllerId == 0) {
            oserr = PlatformMsiAllocate(deviceInterrupt);
            if (oserr != OS_EOK) {
                kfree(systemInterrupt);
                return UUID_INVALID;
            }
            allocatedMsiRoute = 1;
        }
        msiRoute.ControllerId = deviceInterrupt->MsiControllerId;
        msiRoute.HwIrq = deviceInterrupt->MsiHwIrq;
        msiRoute.Index = deviceInterrupt->MsiIndex;
        msiRoute.ParentLine = deviceInterrupt->MsiParentLine;
        msiRoute.Flags = deviceInterrupt->MsiRouteFlags;
        tableIndex = msiRoute.Index;
    } else if (InterruptResolve(deviceInterrupt, flags, &tableIndex) != OS_EOK) {
        ERROR("Failed to resolve the interrupt, invalid flags.");
        kfree(systemInterrupt);
        return UUID_INVALID;
    }

    // Update remaining members now that we resolved
        systemInterrupt->Index                          = (uint16_t)tableIndex;
        systemInterrupt->ParentLine                     = (flags & INTERRUPT_SOFT)
            ? INTERRUPT_NONE : ((flags & INTERRUPT_MSI)
                ? deviceInterrupt->MsiParentLine : deviceInterrupt->Line);
        systemInterrupt->DeviceId                       = deviceInterrupt->DeviceId;
    systemInterrupt->MsiRoute = msiRoute;
    systemInterrupt->Id                            |= tableIndex;
    systemInterrupt->Handler                        = deviceInterrupt->ResourceTable.Handler;
    systemInterrupt->Context                        = deviceInterrupt->Context;
    systemInterrupt->KernelResources.HandleResource = deviceInterrupt->ResourceTable.HandleResource;

    // Trace
    TRACE("Updated line %i:%i for index 0x%" PRIxIN,
            systemInterrupt->ParentLine, deviceInterrupt->Pin, tableIndex);

    // If it's an user interrupt, resolve resources
    if (systemInterrupt->Owner != UUID_INVALID) {
        if (InterruptResolveResources(deviceInterrupt, systemInterrupt) != OS_EOK) {
            ERROR(" > failed to resolve the requested resources");
            if (allocatedMsiRoute) {
                (void)InterruptMsiReleaseTableRoute(&msiRoute);
                PlatformMsiRelease(&msiRoute);
            }
            kfree(systemInterrupt);
            return UUID_INVALID;
        }
    }
    
    // Initialize the table entry?
    SpinlockAcquireIrq(&g_interruptTableLock);

    // Check sharing while holding the same lock used to publish the entry.
    if (flags & INTERRUPT_MSI) {
        if (!g_interruptTable[tableIndex].MsiReserved ||
            g_interruptTable[tableIndex].MsiQuarantined ||
            g_interruptTable[tableIndex].MsiControllerId != msiRoute.ControllerId ||
            g_interruptTable[tableIndex].MsiHwIrq != msiRoute.HwIrq ||
            atomic_load(&g_interruptTable[tableIndex].Descriptor) != NULL) {
            SpinlockReleaseIrq(&g_interruptTableLock);
            if (systemInterrupt->Owner != UUID_INVALID) {
                InterruptReleaseResources(systemInterrupt);
            }
            if (allocatedMsiRoute) {
                (void)InterruptMsiReleaseTableRoute(&msiRoute);
                PlatformMsiRelease(&msiRoute);
            }
            kfree(systemInterrupt);
            return UUID_INVALID;
        }
    } else if (flags & INTERRUPT_EXCLUSIVE) {
        if (atomic_load(&g_interruptTable[tableIndex].Descriptor) != NULL ||
            g_interruptTable[tableIndex].Penalty > 0) {
            ERROR(" > can't gain exclusive access as there exist interrupt for 0x%x", tableIndex);
            SpinlockReleaseIrq(&g_interruptTableLock);
            if (systemInterrupt->Owner != UUID_INVALID) {
                InterruptReleaseResources(systemInterrupt);
            }
            kfree(systemInterrupt);
            return UUID_INVALID;
        }
    } else if (g_interruptTable[tableIndex].Sharable != 1 && g_interruptTable[tableIndex].Penalty > 0) {
        ERROR(" > existing interrupt has exclusive access");
        SpinlockReleaseIrq(&g_interruptTableLock);
        if (systemInterrupt->Owner != UUID_INVALID) {
            InterruptReleaseResources(systemInterrupt);
        }
        kfree(systemInterrupt);
        return UUID_INVALID;
    }

    if (atomic_load(&g_interruptTable[tableIndex].Descriptor) == NULL) {
        g_interruptTable[tableIndex].Sharable = (flags & INTERRUPT_EXCLUSIVE) ? 0 : 1;
    } else {
        atomic_store(&systemInterrupt->Link,
                     atomic_load(&g_interruptTable[tableIndex].Descriptor));
    }
    
    // MSI routes reserved their exclusive table slot before handler binding.
    if (!(flags & INTERRUPT_MSI)) {
        g_interruptTable[tableIndex].Penalty++;
    }
    systemInterrupt->HasPenalty = 1;
    atomic_store(&g_interruptTable[tableIndex].Descriptor, systemInterrupt);

    // Enable the new interrupt
    if (InterruptConfigure(systemInterrupt, 1) != OS_EOK) {
        ERROR("Failed to enable parent line %" PRIiIN "", systemInterrupt->ParentLine);
        atomic_store(&g_interruptTable[tableIndex].Descriptor,
                     atomic_load(&systemInterrupt->Link));
        InterruptRetire(systemInterrupt);
        SpinlockReleaseIrq(&g_interruptTableLock);
        InterruptReclaimRetired();
        return UUID_INVALID;
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    
    TRACE("Interrupt Id 0x%" PRIxIN " (Handler 0x%" PRIxIN ", Context 0x%" PRIxIN ")",
          systemInterrupt->Id, systemInterrupt->Interrupt.ResourceTable.Handler, systemInterrupt->Interrupt.Context);
    return systemInterrupt->Id;
}

static oserr_t
__InterruptUnregister(
    _In_ uuid_t Source,
    _In_ uuid_t Owner,
    _In_ int    CheckOwner)
{
    SystemInterrupt_t* Entry;
    SystemInterrupt_t* Previous   = NULL;
    uint16_t           TableIndex = LOWORD(Source);
    int                Found      = 0;

    // Sanitize parameter
    if (TableIndex >= MAX_SUPPORTED_INTERRUPTS) {
        return OS_EINVALPARAMS;
    }
    
    // Iterate handlers in that table index and unlink the given entry
    SpinlockAcquireIrq(&g_interruptTableLock);
    Entry = atomic_load(&g_interruptTable[TableIndex].Descriptor);
    while (Entry) {
        if (Entry->Id == Source) {
            if (!(Entry->Flags & INTERRUPT_KERNEL)) {
                if ((CheckOwner && Entry->Owner != Owner) ||
                    (!CheckOwner && Entry->Owner != GetCurrentMemorySpaceHandle())) {
                    Previous = Entry;
                    Entry    = atomic_load(&Entry->Link);
                    continue;
                }
            }

            // Marked entry as found
            Found = 1;
            if (Previous == NULL) {
                atomic_store(&g_interruptTable[TableIndex].Descriptor,
                             atomic_load(&Entry->Link));
            } else {
                atomic_store(&Previous->Link, atomic_load(&Entry->Link));
            }
            
            // Mask the line once its last handler is gone. The penalty stays
            // until the grace period, see InterruptReclaimRetired().
            if (atomic_load(&g_interruptTable[TableIndex].Descriptor) == NULL) {
                InterruptConfigure(Entry, 0);
            }
            
            InterruptRetire(Entry);
            break;
        }
        Previous = Entry;
        Entry    = atomic_load(&Entry->Link);
    }
    SpinlockReleaseIrq(&g_interruptTableLock);

    // Entry must not be touched past this point, it may already be reclaimed.
    if (!Found) {
        return OS_ENOENT;
    }

    // A caller in interrupt context cannot wait for a grace period because
    // the current handler may be the reader keeping this entry alive. Normal
    // callers wait, so returning from unregister guarantees that the entry
    // and its context are no longer in use.
    if (!InterruptGetActiveStatus()) {
        while (atomic_load(&g_interruptReaders) != 0) {
            ArchThreadYield();
        }
        InterruptReclaimRetired();
    }
    return OS_EOK;
}

oserr_t
InterruptUnregister(
    _In_ uuid_t Source)
{
    return __InterruptUnregister(Source, UUID_INVALID, 0);
}

oserr_t
InterruptUnregisterOwned(
    _In_ uuid_t Source,
    _In_ uuid_t Owner)
{
    if (Owner == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    return __InterruptUnregister(Source, Owner, 1);
}

oserr_t
InterruptMsiQuiesceRegister(
    _In_ uuid_t Owner,
    _In_ uuid_t EventHandle)
{
    void*  eventResource;
    int    signalEvent = 0;
    oserr_t oserr;

    if (Owner == UUID_INVALID || EventHandle == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    oserr = AcquireHandleOfType(EventHandle, HandleTypeUserEvent, &eventResource);
    if (oserr != OS_EOK) {
        return oserr;
    }

    SpinlockAcquireIrq(&g_msiQuiesceLock);
    if (g_msiQuiesceOwner != UUID_INVALID &&
        (g_msiQuiesceOwner != Owner || g_msiQuiesceEvent != EventHandle)) {
        SpinlockReleaseIrq(&g_msiQuiesceLock);
        (void)DestroyHandle(EventHandle);
        return OS_EPERMISSIONS;
    }
    if (g_msiQuiesceOwner == UUID_INVALID) {
        g_msiQuiesceOwner = Owner;
        g_msiQuiesceEvent = EventHandle;
    } else {
        (void)DestroyHandle(EventHandle);
    }
    signalEvent = g_msiQuiesceHead != NULL;
    SpinlockReleaseIrq(&g_msiQuiesceLock);

    if (signalEvent) {
        (void)UserEventSignal(EventHandle);
    }
    return OS_EOK;
}

void
InterruptMsiQuiesceOwnerExit(
    _In_ uuid_t Owner)
{
    uuid_t eventHandle = UUID_INVALID;

    SpinlockAcquireIrq(&g_msiQuiesceLock);
    if (Owner == g_msiQuiesceOwner) {
        eventHandle = g_msiQuiesceEvent;
        g_msiQuiesceOwner = UUID_INVALID;
        g_msiQuiesceEvent = UUID_INVALID;
    }
    SpinlockReleaseIrq(&g_msiQuiesceLock);

    if (eventHandle != UUID_INVALID) {
        (void)DestroyHandle(eventHandle);
    }
}

oserr_t
InterruptMsiQuiesceEnqueue(
    _In_ const DeviceInterruptQuiesceRequest_t* Request,
    _In_ uuid_t                                  Owner,
    _In_ const InterruptMsiRoute_t*              Routes,
    _In_ uint32_t                                Count,
    _Out_ uuid_t*                                TokenOut)
{
    __InterruptMsiQuiesceEntry_t* entry;
    __InterruptMsiQuiesceEntry_t* iterator;
    uuid_t                       eventHandle;
    uuid_t                       token;

    if (Request == NULL || Routes == NULL || TokenOut == NULL ||
        Owner == UUID_INVALID || Request->DeviceId == UUID_INVALID ||
        Count == 0 || Count > INTERRUPT_MAXVECTORS) {
        return OS_EINVALPARAMS;
    }

    entry = (__InterruptMsiQuiesceEntry_t*)kmalloc(sizeof(*entry));
    if (entry == NULL) {
        return OS_EOOM;
    }
    memset(entry, 0, sizeof(*entry));
    entry->Request = *Request;
    entry->Request.Token = atomic_fetch_add(&g_nextMsiQuiesceToken, 1);
    token = entry->Request.Token;
    entry->Owner = Owner;
    entry->Count = Count;
    for (uint32_t i = 0; i < Count; i++) {
        if (Routes[i].ControllerId == 0 || Routes[i].Index >= MAX_SUPPORTED_INTERRUPTS) {
            kfree(entry);
            return OS_EINVALPARAMS;
        }
        entry->Routes[i] = Routes[i];
    }

    SpinlockAcquireIrq(&g_msiQuiesceLock);
    if (g_msiQuiesceHead == NULL) {
        g_msiQuiesceHead = entry;
    } else {
        iterator = g_msiQuiesceHead;
        while (iterator->Next != NULL) {
            iterator = iterator->Next;
        }
        iterator->Next = entry;
    }
    eventHandle = g_msiQuiesceEvent;
    *TokenOut = token;
    SpinlockReleaseIrq(&g_msiQuiesceLock);

    if (eventHandle != UUID_INVALID) {
        (void)UserEventSignal(eventHandle);
    }
    return OS_EOK;
}

oserr_t
InterruptMsiQuiesceNext(
    _In_  uuid_t                           Owner,
    _Out_ DeviceInterruptQuiesceRequest_t* RequestOut)
{
    if (RequestOut == NULL) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_msiQuiesceLock);
    if (Owner != g_msiQuiesceOwner) {
        SpinlockReleaseIrq(&g_msiQuiesceLock);
        return OS_EPERMISSIONS;
    }
    if (g_msiQuiesceHead == NULL) {
        SpinlockReleaseIrq(&g_msiQuiesceLock);
        return OS_ENOENT;
    }
    *RequestOut = g_msiQuiesceHead->Request;
    SpinlockReleaseIrq(&g_msiQuiesceLock);
    return OS_EOK;
}

oserr_t
InterruptMsiQuiesceFinish(
    _In_ uuid_t Owner,
    _In_ uuid_t Token)
{
    __InterruptMsiQuiesceEntry_t* entry;
    __InterruptMsiQuiesceEntry_t* previous = NULL;

    SpinlockAcquireIrq(&g_msiQuiesceLock);
    if (Owner != g_msiQuiesceOwner) {
        SpinlockReleaseIrq(&g_msiQuiesceLock);
        return OS_EPERMISSIONS;
    }
    entry = g_msiQuiesceHead;
    while (entry != NULL && entry->Request.Token != Token) {
        previous = entry;
        entry = entry->Next;
    }
    if (entry == NULL) {
        SpinlockReleaseIrq(&g_msiQuiesceLock);
        return OS_ENOENT;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    for (uint32_t i = 0; i < entry->Count; i++) {
        InterruptTableEntry_t* tableEntry = &g_interruptTable[entry->Routes[i].Index];
        if (!tableEntry->MsiQuarantined || !tableEntry->MsiReserved ||
            tableEntry->MsiControllerId != entry->Routes[i].ControllerId ||
            tableEntry->MsiHwIrq != entry->Routes[i].HwIrq ||
            atomic_load(&tableEntry->Descriptor) != NULL ||
            tableEntry->Penalty == 0) {
            SpinlockReleaseIrq(&g_interruptTableLock);
            SpinlockReleaseIrq(&g_msiQuiesceLock);
            return OS_EBUSY;
        }
        for (uint32_t j = i + 1; j < entry->Count; j++) {
            if (entry->Routes[i].Index == entry->Routes[j].Index) {
                SpinlockReleaseIrq(&g_interruptTableLock);
                SpinlockReleaseIrq(&g_msiQuiesceLock);
                return OS_EINVALPARAMS;
            }
        }
    }
    for (uint32_t i = 0; i < entry->Count; i++) {
        InterruptTableEntry_t* tableEntry = &g_interruptTable[entry->Routes[i].Index];
        tableEntry->MsiQuarantined = 0;
        if (entry->Routes[i].Flags & INTERRUPT_MSI_ROUTE_PARENT_RESERVED) {
            tableEntry->MsiHwIrq = UINT32_MAX;
        } else {
            tableEntry->MsiReserved = 0;
            tableEntry->MsiControllerId = 0;
            tableEntry->MsiHwIrq = 0;
            tableEntry->Penalty--;
            if (tableEntry->Penalty == 0) {
                tableEntry->Sharable = 0;
            }
        }
    }
    SpinlockReleaseIrq(&g_interruptTableLock);

    for (uint32_t i = 0; i < entry->Count; i++) {
        PlatformMsiRelease(&entry->Routes[i]);
    }

    if (previous == NULL) {
        g_msiQuiesceHead = entry->Next;
    } else {
        previous->Next = entry->Next;
    }
    SpinlockReleaseIrq(&g_msiQuiesceLock);
    kfree(entry);
    return OS_EOK;
}

SystemInterrupt_t*
InterruptGet(
        _In_ uuid_t Source)
{
    SystemInterrupt_t* Iterator;
    uint16_t           TableIndex = LOWORD(Source);

    if (TableIndex >= MAX_SUPPORTED_INTERRUPTS) {
        return NULL;
    }

    SpinlockAcquireIrq(&g_interruptTableLock);
    Iterator = atomic_load(&g_interruptTable[TableIndex].Descriptor);
    while (Iterator != NULL) {
        if (Iterator->Id == Source) {
            atomic_fetch_add(&Iterator->References, 1);
            SpinlockReleaseIrq(&g_interruptTableLock);
            return Iterator;
        }
        Iterator = atomic_load(&Iterator->Link);
    }
    SpinlockReleaseIrq(&g_interruptTableLock);
    return NULL;
}

void
InterruptPut(
        _In_ SystemInterrupt_t* Interrupt)
{
    if (Interrupt == NULL) {
        return;
    }

    atomic_fetch_sub(&Interrupt->References, 1);
    InterruptReclaimRetired();
}

void
InterruptSetActiveStatus(
    _In_ int Active)
{
    SystemCpuCore_t* core = CpuCoreCurrent();
    SystemCpuState_t cpuState = CpuCoreState(core);
    cpuState &= ~(CpuStateInterruptActive);
    if (Active) {
        cpuState |= CpuStateInterruptActive;
    }
    CpuCoreSetState(core, cpuState);
}

int
InterruptGetActiveStatus(void)
{
    SystemCpuState_t State = CpuCoreState(CpuCoreCurrent());
    return (State & CpuStateInterruptActive) == 0 ? 0 : 1;
}

Context_t*
InterruptHandle(
    _In_  Context_t* context,
    _In_  int        tableIndex)
{
    uint32_t           initialPriority = InterruptsGetPriority();
    int                interruptSource = INTERRUPT_NONE;
    irqstatus_t        interruptStatus;
    SystemInterrupt_t* entry;

    InterruptsSetPriority(tableIndex);
    CpuCoreEnterInterrupt(context, initialPriority);

    // The interrupt path deliberately does not acquire the table lock.
    // Entering the RCU read-side critical section before loading Descriptor
    // keeps every descriptor, handler, and context used below alive until
    // the complete dispatch walk has finished.
    InterruptReadEnter();
    entry = atomic_load(&g_interruptTable[tableIndex].Descriptor);
    while (entry != NULL) {
        if (entry->Flags & INTERRUPT_KERNEL) {
            interruptStatus = entry->Handler(GetFastInterruptTable(), entry->Context);
        } else {
            interruptStatus = entry->KernelResources.Handler(GetFastInterruptTable(), &entry->KernelResources);
        }

        if (interruptStatus == IRQSTATUS_HANDLED) {
            interruptSource = entry->ParentLine;
            break;
        }
        entry = atomic_load(&entry->Link);
    }
    InterruptReadExit();
    
    InterruptsAcknowledge(interruptSource, tableIndex);
    return CpuCoreExitInterrupt(context, initialPriority);
}
