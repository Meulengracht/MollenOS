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

#include <firmware/tree.h>
#include <firmware/platform.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

#include "private.h"
#include "ownership.h"

struct __PlatformWalk {
    struct PlatformBus*    Bus;
    struct PlatformEntry** Tail;
    struct FdtTree*        Tree;
    struct PlatformEntry*  Parents[FDT_MAX_DEPTH];
    oserr_t                Status;
};

static int
__IsPlatformNode(
    _In_ const struct __PlatformWalk* walk,
    _In_ const struct FdtResources*   node,
    _In_ int                          depth)
{
    const struct PlatformEntry* parent;

    if (!depth) {
        TRACE("__IsPlatformNode: skipping root node %u", node->NodeOffset);
        return 0;
    }
    if (node->Disabled) {
        TRACE("__IsPlatformNode: skipping disabled node %u", node->NodeOffset);
        return 0;
    }
    if (node->ParentDisabled) {
        TRACE("__IsPlatformNode: skipping node %u with a disabled parent", node->NodeOffset);
        return 0;
    }

    // At depths greater than one, the parent must be a bus accepted by this walk.
    // This excludes PCI/RP1 buses, buses managed elsewhere and parents whose
    // resources cannot be described as a platform device.
    if (depth > 1) {
        parent = walk->Parents[depth - 1];
        if (parent == NULL) {
            TRACE("__IsPlatformNode: skipping node %u without an accepted parent at depth %d",
                  node->NodeOffset, depth - 1);
            return 0;
        }
        if (!parent->IsBus) {
            TRACE("__IsPlatformNode: skipping node %u whose parent %u is not a bus",
                  node->NodeOffset, parent->Firmware.FirmwareNode);
            return 0;
        }
    }
    if (node->Compatible == NULL) {
        TRACE("__IsPlatformNode: skipping node %u without a compatible property", node->NodeOffset);
        return 0;
    }
    if (PlatformNodeIsManaged(walk->Tree, node)) {
        TRACE("__IsPlatformNode: skipping node %u managed elsewhere", node->NodeOffset);
        return 0;
    }
    return 1;
}

static void
__CollectDevice(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _InOut_ void*                      context)
{
    struct __PlatformWalk*     walk = context;
    const struct FdtResources* node = &nodes[depth];
    struct PlatformEntry*      entry;
    PlatformDevice_t           device;
    oserr_t                    status;

    walk->Parents[depth] = NULL;
    if (walk->Status != OS_EOK) {
        return;
    }
    if (!__IsPlatformNode(walk, node, depth)) {
        return;
    }

    status = FdtDescribePlatformDevice(walk->Tree, nodes, depth, &device);
    if (status == OS_ENOTSUPPORTED) {
        WARNING("__CollectDevice: skipping unsupported description for node %u (%s) (%u)",
                node->NodeOffset, node->Name, status);
        return;
    }

    if (status != OS_EOK) {
        ERROR("__CollectDevice: malformed resources for node %u (%s) (%u)",
              node->NodeOffset, node->Name, status);
        walk->Status = status;
        return;
    }

    if (!node->IsSimpleBus) {
        device.Pending = PLATFORM_DEVICE_PENDING_ACTIVATION | PLATFORM_DEVICE_PENDING_DMA;
        if (device.InterruptCount != 0) {
            device.Pending |= PLATFORM_DEVICE_PENDING_INTERRUPTS;
        }
    }

    entry = calloc(1, sizeof(struct PlatformEntry));
    if (entry == NULL) {
        ERROR("__CollectDevice: failed to allocate entry for node %u (%s) (%u)",
              node->NodeOffset, node->Name, OS_EOOM);
        walk->Status = OS_EOOM;
        return;
    }

    device.Base.Identification.Description = strdup(node->Name);
    if (device.Base.Identification.Description == NULL) {
        ERROR("__CollectDevice: failed to copy description for node %u (%s) (%u)",
              node->NodeOffset, node->Name, OS_EOOM);
        free(entry);
        walk->Status = OS_EOOM;
        return;
    }

    entry->Firmware = device;
    entry->ParentNode = depth == 1 ? UINT32_MAX : nodes[depth - 1].NodeOffset;
    entry->IsBus = node->IsSimpleBus;
    entry->Bus = walk->Bus;
    walk->Parents[depth] = entry;

    // The tree walk visits parents before children, so append in walk order.
    *walk->Tail = entry;
    walk->Tail = &entry->Next;
}

oserr_t
PlatformCollectDevices(
    _InOut_ struct PlatformBus* bus,
    _In_    struct FdtTree*     tree)
{
    oserr_t               status;
    struct __PlatformWalk walk = {
        .Bus = bus,
        .Tail = &bus->Entries,
        .Tree = tree
    };

    status = FdtWalkResources(tree, __CollectDevice, &walk);
    if (status != OS_EOK) {
        ERROR("PlatformCollectDevices: resource walk failed for bus %u (%u)",
              bus->DeviceId, status);
        return status;
    }
    return walk.Status;
}
