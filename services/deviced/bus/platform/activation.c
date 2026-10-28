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

#include <stdlib.h>

#include "private.h"

/**
 * @brief Registered dependency callbacks and the number of active tokens.
 * The bus lock protects this list and References. A nonzero count prevents
 * unregistering the callbacks while an activation still needs them.
 */
struct PlatformDependencyProvider {
    struct PlatformDependencyProvider*         Next;
    uint32_t                                   Node;
    uint32_t                                   References;
    const struct PlatformDependencyOperations* Operations;
    void*                                      Context;
};

struct __PlatformDependencyLease {
    struct PlatformDependencyProvider* Provider;
    void*                              Token;
};

/**
 * @brief Activation owner and dependency tokens in preparation order.
 * Count includes only successful requests. Failed cleanup leaves the remaining
 * tokens here for a later retry, with Prepared cleared.
 */
struct PlatformActivation {
    uuid_t                           Principal;
    int                              Prepared;
    uint32_t                         Count;
    struct __PlatformDependencyLease Leases[FDT_PLATFORM_MAX_DEPENDENCIES];
};

static struct PlatformDependencyProvider*
__PlatformDependencyFind(
    _In_ struct PlatformBus* bus,
    _In_ uint32_t            node)
{
    struct PlatformDependencyProvider* provider;

    for (provider = bus->Dependencies; provider != NULL; provider = provider->Next) {
        if (provider->Node == node) {
            return provider;
        }
    }
    return NULL;
}

oserr_t
PlatformDependencyRegister(
    _In_ struct PlatformBus*                        bus,
    _In_ uint32_t                                   providerNode,
    _In_ const struct PlatformDependencyOperations* operations,
    _In_ void*                                      context)
{
    const struct FdtResources*         node = FdtTreeNode(bus->Tree, providerNode);
    struct PlatformDependencyProvider* provider;
    oserr_t                            status = OS_EOK;

    if (operations == NULL || operations->Acquire == NULL || operations->Release == NULL) {
        return OS_EINVALPARAMS;
    }

    if (node == NULL || node->Malformed || node->ParentMalformed) {
        return OS_EINVALPARAMS;
    }

    if (node->Disabled || node->ParentDisabled) {
        return OS_ENOENT;
    }

    usched_mtx_lock(&bus->Lock);
    if (bus->Removing) {
        status = OS_EBUSY;
    } else if (__PlatformDependencyFind(bus, providerNode) != NULL) {
        status = OS_EEXISTS;
    } else {
        provider = calloc(1, sizeof(struct PlatformDependencyProvider));
        if (provider == NULL) {
            status = OS_EOOM;
        } else {
            provider->Node = providerNode;
            provider->Operations = operations;
            provider->Context = context;
            provider->Next = bus->Dependencies;
            bus->Dependencies = provider;
        }
    }
    usched_mtx_unlock(&bus->Lock);
    return status;
}

oserr_t
PlatformDependencyUnregister(
    _In_ struct PlatformBus* bus,
    _In_ uint32_t            providerNode)
{
    struct PlatformDependencyProvider** link;
    struct PlatformDependencyProvider*  provider;
    oserr_t                             status = OS_ENOENT;

    usched_mtx_lock(&bus->Lock);
    for (link = &bus->Dependencies; *link != NULL; link = &(*link)->Next) {
        provider = *link;

        if (provider->Node != providerNode) {
            continue;
        }

        if (provider->References) {
            status = OS_EBUSY;
        } else {
            *link = provider->Next;
            free(provider);
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

        status = lease->Provider->Operations->Release(
            lease->Provider->Context,
            lease->Token
        );
        if (status != OS_EOK) {
            // Preserve this and all earlier dependencies. Turning off a parent
            // while a child release failed would violate dependency ordering.
            return status;
        }
        lease->Provider->References--;
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
    struct FdtPlatformDependencies     plan;
    struct PlatformDependencyProvider* providers[FDT_PLATFORM_MAX_DEPENDENCIES] = { 0 };
    struct PlatformActivation*         activation;
    struct __PlatformDependencyLease*  lease;
    oserr_t                            status;
    oserr_t                            cleanup;
    uint32_t                           i;

    status = FdtPlatformDependencies(entry->Bus->Tree, entry->Firmware.FirmwareNode, &plan);
    if (status != OS_EOK) {
        return status;
    }

    // All mutable dependencies must have callbacks before hardware changes.
    // If a dependency driver has not registered yet, preparation can be retried
    // after it starts.
    for (i = 0; i < plan.Count; i++) {
        if (plan.Entries[i].Immutable) {
            continue;
        }

        providers[i] = __PlatformDependencyFind(entry->Bus, plan.Entries[i].Provider);
        if (providers[i] == NULL) {
            return OS_ENOTSUPPORTED;
        }

        if (providers[i]->References == UINT32_MAX) {
            return OS_EOVERFLOW;
        }
    }

    activation = calloc(1, sizeof(struct PlatformActivation));
    if (activation == NULL) {
        return OS_EOOM;
    }

    activation->Principal = principal;
    entry->Activation = activation;

    for (i = 0; i < plan.Count; i++) {
        if (providers[i] == NULL) {
            continue;
        }

        lease = &activation->Leases[activation->Count];
        lease->Provider = providers[i];
        if (providers[i]->References == UINT32_MAX) {
            cleanup = __PlatformActivationRelease(entry);
            return cleanup == OS_EOK ? OS_EOVERFLOW : cleanup;
        }

        status = providers[i]->Operations->Acquire(
            providers[i]->Context,
            &plan.Entries[i],
            &lease->Token
        );
        if (status != OS_EOK) {
            cleanup = __PlatformActivationRelease(entry);
            return cleanup == OS_EOK ? status : cleanup;
        }
        providers[i]->References++;
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
        return OS_EPERMISSIONS;
    }

    if (stage != DevicePrepare && stage != DeviceStart && stage != DeviceStop) {
        return OS_EINVALPARAMS;
    }

    if (entry->Activation != NULL && entry->Activation->Principal != principal) {
        return OS_EPERMISSIONS;
    }

    if (stage == DeviceStart) {
        if (!PlatformIsActivationReady(entry)) {
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
            return OS_EBUSY;
        }

        for (i = 0; i < entry->Firmware.InterruptCount; i++) {
            if (entry->Interrupts[i].Allocation != UUID_INVALID) {
                return OS_EBUSY;
            }
        }

        status = __PlatformActivationRelease(entry);
        if (status != OS_EOK) {
            return status;
        }
    }
    return stage == DeviceStop ? OS_EOK : __PlatformActivationPrepare(entry, principal);
}
