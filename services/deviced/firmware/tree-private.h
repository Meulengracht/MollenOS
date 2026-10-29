/**
 * Copyright 2026, Philip Meulengracht
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

#ifndef __FIRMWARE_TREE_PRIVATE_H__
#define __FIRMWARE_TREE_PRIVATE_H__

#include <firmware/tree.h>
#include <stdatomic.h>

/**
 * @brief Cached node in firmware order, with each parent before its children.
 * Parent is the parent entry's index. End is the first index after this node's
 * subtree, and Depth is zero for the root.
 */
struct __FdtEntry {
    struct FdtResources Resources;
    size_t              Parent;
    size_t              End;
    int                 Depth;
};

/**
 * @brief Parsed node records and a phandle lookup table, sorted by phandle.
 * Once construction succeeds, only the reference count changes. Console points
 * into Entries when the stdout node was resolved, or is NULL otherwise.
 */
struct FdtTree {
    struct __FdtEntry*         Entries;
    size_t                     Count;
    struct __FdtEntry**        Providers;
    size_t                     ProviderCount;
    const struct FdtResources* Console;
    atomic_uint                References;
};

/**
 * @brief Decode one node after the records on its path from the root are ready.
 *
 * @param view Node whose properties will be decoded.
 * @param nodes Path records with decoded parents in slots before depth and a
 *              writable slot at depth for the current node.
 * @param depth Zero-based depth of the current node.
 */
void
FdtDecodeResources(
    _In_    const struct FdtNode* view,
    _InOut_ struct FdtResources*  nodes,
    _In_    int                   depth);

#endif //!__FIRMWARE_TREE_PRIVATE_H__
