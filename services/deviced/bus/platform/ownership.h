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

#ifndef __DEVICED_PLATFORM_OWNERSHIP_H__
#define __DEVICED_PLATFORM_OWNERSHIP_H__

#include <firmware/tree.h>

/**
 * @brief Check whether the kernel or another bus manages the firmware node.
 *
 * This controls publication only. It does not decode firmware resources or
 * manage runtime access.
 * Rules match individual nodes, without restricting discovery to a board model.
 *
 * @param tree Shared inventory containing node and the resolved console.
 * @param node Cached node or traversal copy from tree.
 * @return Nonzero when generic platform publication must skip the node.
 */
int
PlatformNodeIsManaged(
    _In_ const struct FdtTree*      tree,
    _In_ const struct FdtResources* node);

#endif
