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

#include <ddk/utils.h>
#include <stdlib.h>

#include "private.h"

struct PlatformDependencyRegistration {
    struct PlatformDependencyRegistration*         Next;
    uint32_t                                   Node;
    uint32_t                                   References;
    const struct PlatformDependencyOperations* Operations;
    void*                                      Context;
};

struct __PlatformDependencyLease {
    struct PlatformDependencyRegistration* Registration;
    void*                                  Token;
};

struct PlatformActivation {
    uuid_t                           Principal;
    int                              Prepared;
    uint32_t                         Count;
    struct __PlatformDependencyLease Leases[FDT_PLATFORM_MAX_DEPENDENCIES];
};

static struct PlatformDependencyRegistration*
__PlatformDependencyFind(
    _In_ struct PlatformBus* bus,
    _In_ uint32_t            node)
{
    struct PlatformDependencyRegistration* registration;

    for (registration = bus->Dependencies; registration != NULL; registration = registration->Next) {
        if (registration->Node == node) {
            return registration;
        }
    }
    return NULL;
}

oserr_t
PlatformDependencyRegister(
    _In_ struct PlatformBus*                        bus,
    _In_ uint32_t                                   hostNode,
    _In_ const struct PlatformDependencyOperations* operations,
    _In_ void*                                      context)
{
    const struct FdtResources*             node = FdtTreeNode(bus->Tree, hostNode);
    struct PlatformDependencyRegistration* registration;
    oserr_t                                status = OS_EOK;

    if (operations == NULL || operations->Acquire == NULL || operations->Release == NULL) {
        ERROR("PlatformDependencyRegister: node %u has invalid dependency callbacks (%u)",
              hostNode, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }

    if (node == NULL || node->Malformed || node->ParentMalformed) {
        ERROR("PlatformDependencyRegister: node %u is missing or malformed (%u)",
              hostNode, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }

    if (node->Disabled || node->ParentDisabled) {
        TRACE("PlatformDependencyRegister: node %u or its parent is disabled", hostNode);
        return OS_ENOENT;
    }

    usched_mtx_lock(&bus->Lock);
    if (bus->Removing) {
        WARNING("PlatformDependencyRegister: node %u cannot register during bus removal (%u)",
                hostNode, OS_EBUSY);
        status = OS_EBUSY;
    } else if (__PlatformDependencyFind(bus, hostNode) != NULL) {
        WARNING("PlatformDependencyRegister: node %u already has dependency callbacks (%u)",
                hostNode, OS_EEXISTS);
        status = OS_EEXISTS;
    } else {
        registration = calloc(1, sizeof(struct PlatformDependencyRegistration));
        if (registration == NULL) {
            ERROR("PlatformDependencyRegister: node %u could not allocate dependency state (%u)",
                  hostNode, OS_EOOM);
            status = OS_EOOM;
        } else {
            registration->Node = hostNode;
            registration->Operations = operations;
            registration->Context = context;
            registration->Next = bus->Dependencies;
            bus->Dependencies = registration;
        }
    }
    usched_mtx_unlock(&bus->Lock);
    return status;
}

oserr_t
PlatformDependencyUnregister(
    _In_ struct PlatformBus* bus,
    _In_ uint32_t            hostNode)
{
    struct PlatformDependencyRegistration** link;
    struct PlatformDependencyRegistration*  registration;
    oserr_t                             status = OS_ENOENT;

    usched_mtx_lock(&bus->Lock);
    for (link = &bus->Dependencies; *link != NULL; link = &(*link)->Next) {
        registration = *link;

        if (registration->Node != hostNode) {
            continue;
        }

        if (registration->References) {
            WARNING("PlatformDependencyUnregister: node %u still has %u dependency references (%u)",
                    hostNode, registration->References, OS_EBUSY);
            status = OS_EBUSY;
        } else {
            *link = registration->Next;
            free(registration);
            status = OS_EOK;
        }
        break;
    }
    usched_mtx_unlock(&bus->Lock);
    return status;
}

static oserr_t
__PlatformActivationRelease(
    _InOut_ struct PlatformEntry* entry)
{
    struct PlatformActivation*        activation = entry->Activation;
    struct __PlatformDependencyLease* lease;
    oserr_t                           status;

    activation->Prepared = 0;
    while (activation->Count) {
        lease = &activation->Leases[activation->Count - 1];

        status = lease->Registration->Operations->Release(
            lease->Registration->Context,
            lease->Token
        );
        if (status != OS_EOK) {
            ERROR("__PlatformActivationRelease: node %u dependency %u node %u release failed (%u)",
                  entry->Firmware.FirmwareNode, activation->Count - 1,
                  lease->Registration->Node, status);
            // Preserve this and all earlier dependencies. Turning off a parent
            // while a child release failed would violate dependency ordering.
            return status;
        }
        lease->Registration->References--;
        activation->Count--;
    }

    free(activation);
    entry->Activation = NULL;
    return OS_EOK;
}

static oserr_t
__PlatformActivationPrepare(
    _InOut_ struct PlatformEntry* entry,
    _In_    uuid_t                principal)
{
    struct FdtPlatformDependencies         dependencies;
    struct PlatformDependencyRegistration* registrations[FDT_PLATFORM_MAX_DEPENDENCIES] = { 0 };
    struct PlatformActivation*             activation;
    struct __PlatformDependencyLease*      lease;
    oserr_t                                status;
    oserr_t                                cleanup;

    status = FdtPlatformDependencies(entry->Bus->Tree, entry->Firmware.FirmwareNode, &dependencies);
    if (status != OS_EOK) {
        ERROR("__PlatformActivationPrepare: node %u could not build dependency plan (%u)",
              entry->Firmware.FirmwareNode, status);
        return status;
    }

    // All mutable dependencies must have callbacks before hardware changes.
    // If a dependency driver has not registered yet, preparation can be retried
    // after it starts.
    for (uint32_t i = 0; i < dependencies.Count; i++) {
        if (dependencies.Entries[i].Immutable) {
            continue;
        }

        registrations[i] = __PlatformDependencyFind(entry->Bus, dependencies.Entries[i].NodeOffset);
        if (registrations[i] == NULL) {
            WARNING("__PlatformActivationPrepare: node %u dependency %u node %u has no callbacks (%u)",
                    entry->Firmware.FirmwareNode, i, dependencies.Entries[i].NodeOffset, OS_ENOTSUPPORTED);
            return OS_ENOTSUPPORTED;
        }

        if (registrations[i]->References == UINT32_MAX) {
            ERROR("__PlatformActivationPrepare: node %u dependency %u node %u reference count overflow (%u)",
                  entry->Firmware.FirmwareNode, i, registrations[i]->Node, OS_EOVERFLOW);
            return OS_EOVERFLOW;
        }
    }

    activation = calloc(1, sizeof(struct PlatformActivation));
    if (activation == NULL) {
        ERROR("__PlatformActivationPrepare: node %u could not allocate activation state (%u)",
              entry->Firmware.FirmwareNode, OS_EOOM);
        return OS_EOOM;
    }

    activation->Principal = principal;
    entry->Activation = activation;

    for (uint32_t i = 0; i < dependencies.Count; i++) {
        if (registrations[i] == NULL) {
            continue;
        }

        lease = &activation->Leases[activation->Count];
        lease->Registration = registrations[i];
        if (registrations[i]->References == UINT32_MAX) {
            ERROR("__PlatformActivationPrepare: node %u dependency %u node %u reference count overflow (%u)",
                  entry->Firmware.FirmwareNode, i, registrations[i]->Node, OS_EOVERFLOW);
            cleanup = __PlatformActivationRelease(entry);
            if (cleanup != OS_EOK) {
                return cleanup;
            }
            return OS_EOVERFLOW;
        }

        status = registrations[i]->Operations->Acquire(
            registrations[i]->Context,
            &dependencies.Entries[i],
            &lease->Token
        );
        if (status != OS_EOK) {
            ERROR("__PlatformActivationPrepare: node %u dependency %u node %u request failed (%u)",
                  entry->Firmware.FirmwareNode, i, registrations[i]->Node, status);
            cleanup = __PlatformActivationRelease(entry);
            if (cleanup != OS_EOK) {
                return cleanup;
            }
            return status;
        }
        registrations[i]->References++;
        activation->Count++;
    }

    activation->Prepared = 1;
    return OS_EOK;
}

int
PlatformIsActivationReady(
    _In_ const struct PlatformEntry* entry)
{
    return entry->Activation != NULL && entry->Activation->Prepared;
}

int
PlatformIsRuntimeBusy(
    _In_ const struct PlatformEntry* entry)
{
    unsigned int i;

    if (entry->Activation != NULL) {
        return 1;
    }
    for (i = 0; i < entry->Firmware.InterruptCount; i++) {
        if (entry->Interrupts[i].Allocation != UUID_INVALID) {
            return 1;
        }
    }
    return 0;
}

oserr_t
PlatformActivationControl(
    _InOut_ struct PlatformEntry* entry,
    _In_    uuid_t                principal,
    _In_    enum DeviceActivation stage)
{
    oserr_t      status;
    unsigned int i;

    if (principal == UUID_INVALID) {
        ERROR("PlatformActivationControl: node %u stage %u has no valid principal (%u)",
              entry->Firmware.FirmwareNode, stage, OS_EPERMISSIONS);
        return OS_EPERMISSIONS;
    }

    if (stage != DevicePrepare && stage != DeviceStart && stage != DeviceStop) {
        ERROR("PlatformActivationControl: node %u has invalid activation stage %u (%u)",
              entry->Firmware.FirmwareNode, stage, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }

    if (entry->Activation != NULL && entry->Activation->Principal != principal) {
        ERROR("PlatformActivationControl: node %u stage %u requested by a different principal (%u)",
              entry->Firmware.FirmwareNode, stage, OS_EPERMISSIONS);
        return OS_EPERMISSIONS;
    }

    if (stage == DeviceStart) {
        if (!PlatformIsActivationReady(entry)) {
            WARNING("PlatformActivationControl: node %u cannot start before preparation (%u)",
                    entry->Firmware.FirmwareNode, OS_EBUSY);
            return OS_EBUSY;
        }
        // Native masters have no generic bus-master bit. The peripheral driver
        // starts its engine only after buffers and interrupt handlers are ready.
        return OS_EOK;
    }

    if (stage == DevicePrepare && PlatformIsActivationReady(entry)) {
        return OS_EOK;
    }

    if (entry->Activation != NULL) {
        if (entry->DmaReferences != 0) {
            WARNING("PlatformActivationControl: node %u stage %u still has %u DMA references (%u)",
                    entry->Firmware.FirmwareNode, stage, entry->DmaReferences, OS_EBUSY);
            return OS_EBUSY;
        }

        for (i = 0; i < entry->Firmware.InterruptCount; i++) {
            if (entry->Interrupts[i].Allocation != UUID_INVALID) {
                WARNING("PlatformActivationControl: node %u stage %u interrupt %u is still allocated (%u)",
                        entry->Firmware.FirmwareNode, stage, i, OS_EBUSY);
                return OS_EBUSY;
            }
        }

        status = __PlatformActivationRelease(entry);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (stage == DeviceStop) {
        return OS_EOK;
    }

    status = __PlatformActivationPrepare(entry, principal);
    if (status != OS_EOK) {
        return status;
    }
    return OS_EOK;
}
