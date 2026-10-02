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
 * Built from docs/specifications/devicetree-specification-v0.4.pdf
 */

#include <arch/utils.h>
#include <devicetree.h>

static DeviceTree_t* g_deviceTree;

const DeviceTree_t*
DeviceTreeGet(void)
{
    return g_deviceTree;
}

oserr_t
DeviceTreeParseFull(
    _In_ const void* deviceTree,
    _In_ uint32_t    deviceTreeSize)
{
    DeviceTree_t* tree;
    oserr_t       oserr;

    oserr = DeviceTreeCreate(deviceTree, deviceTreeSize, &tree);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Components are registered only after the complete tree is validated.
    // Platform discovery resolves all required resources before publication;
    // any later allocation failure is fatal to boot, not a partial topology.
#ifdef __aarch64__
    oserr = ArchDeviceTreeInitialize(tree);
    if (oserr != OS_EOK) {
        DeviceTreeDestroy(tree);
        return oserr;
    }
#endif
    g_deviceTree = tree;
    return OS_EOK;
}
