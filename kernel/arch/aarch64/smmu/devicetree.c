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
 * ARM SMMU Device Tree Discovery
 * Entry point for building an ArmSMMUDiscovery_t table from a Device Tree.
 * Unit parsing lives in devicetree-unit.c and route parsing in devicetree-route.c.
 */

#include "devicetree-private.h"
#include <heap.h>
#include <debug.h>
#include <log.h>
#include <string.h>

oserr_t
ArmSMMUDiscoverDeviceTree(
    _In_    const DeviceTree_t*  tree,
    _InOut_ ArmSMMUDiscovery_t** discoveryOut)
{
    struct ArmSMMUTreeParser parser = { .Tree = tree };
    const DeviceTreeNode_t*  node = NULL;
    oserr_t                  status;

    if (tree == NULL || tree->Root == NULL || discoveryOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*discoveryOut != NULL) {
        return OS_EBUSY;
    }
    
    parser.Discovery = kmalloc(sizeof(ArmSMMUDiscovery_t));
    if (parser.Discovery == NULL) {
        return OS_EOOM;
    }
    
    memset(parser.Discovery, 0, sizeof(ArmSMMUDiscovery_t));
    parser.Discovery->DeviceTree = tree;
    
    // Pass 1: gather all SMMU controllers first so device phandle references
    // resolve even when a device appears earlier in the tree than its SMMU.
    status = ArmSMMUDeviceTreeCollectUnits(&parser);
    if (status != OS_EOK) {
        goto failed;
    }

    // Pass 2: read every PCI host bridge and platform device DMA route.
    for (node = tree->Nodes; node != NULL; node = node->NextNode) {
        status = ArmSMMUDeviceTreeReadRoute(&parser, node);
        if (status != OS_EOK) {
            goto failed;
        }
    }

    *discoveryOut = parser.Discovery;
    return OS_EOK;

failed:
    // Device Tree readers report OS_EOVERFLOW when a numeric cell overflows.
    // Convert that into OS_EINVALPARAMS so callers see a malformed tree error.
    if (status == OS_EOVERFLOW) {
        status = OS_EINVALPARAMS;
    }

    ERROR("SMMU DT: discovery failed at node %s (%u)", node == NULL ? "inventory" : node->Name, status);
    ArmSMMUDiscoveryDestroy(&parser.Discovery);
    return status;
}
