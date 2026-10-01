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

// Include all the components we will construct during the full
// DTB parsing.
#include <component/cpu.h>
#include <component/memory.h>
#include <component/ic.h>

#include <devicetree.h>
#include "private.h"

oserr_t
DeviceTreeParseFull(
    _In_  const void* deviceTree,
    _In_  uint32_t    deviceTreeSize)
{
    // TODO: Implement full device tree parsing and system component registration.
    return OS_EOK;
}
