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

#ifndef DEVICED_FIRMWARE_READER_H
#define DEVICED_FIRMWARE_READER_H

#include <fdt/reader.h>

/** Describes one device-tree node without copying its strings or properties.
 * A node is an entry describing hardware or its configuration. You may copy
 * this structure, but its pointers still refer to the original firmware data.
 * Keep that data mapped and unchanged until all copies are no longer used.
 * NodeOffset is a byte offset within the tree's structure block. Phandle is
 * an optional numeric ID used by other nodes to refer to this node. */
struct FdtNode {
    const char* Name;
    uint32_t NodeOffset;
    uint32_t Phandle;
    int Disabled;
    int Malformed;
    int AncestorDisabled;
    int AncestorMalformed;
    const uint8_t* Properties;
    uint32_t PropertiesLength;
    const char* Strings;
    uint32_t StringsLength;
};

typedef void (*FdtNodeFn)(const struct FdtNode* nodes, int depth, void* context);

/**
 * @brief Check the tree's format, then call the visitor for each node.
 *
 * @param blob Firmware device-tree data, kept mapped and unchanged during use.
 * @param length Available size of blob in bytes.
 * @param visitor Callback receiving an array from the root to the current node
 *                and the current node's index (depth). NULL checks the tree only.
 *                The array is temporary; copy any records needed later. Their
 *                strings and property bytes still point into blob.
 * @param context Caller data passed to visitor.
 * @return OS_EOK on success, or an error if the tree's format is invalid.
 */
oserr_t
FdtWalkNodes(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ FdtNodeFn visitor,
    _InOut_ void* context);

/**
 * @brief Find the enabled node with the requested firmware ID.
 *
 * Searches the whole tree, so references may point to nodes listed later.
 * Duplicate IDs are rejected even if one of the matching nodes is disabled.
 *
 * @param blob Firmware device-tree data, kept mapped and unchanged during use.
 * @param length Available size of blob in bytes.
 * @param phandle Numeric node ID to find.
 * @param node Receives the node on success; its pointers refer to blob.
 * @return OS_EOK on success, OS_ENOENT if missing or disabled, or an error for
 *         invalid input, duplicate IDs, or malformed node or parent data.
 */
oserr_t
FdtFindNode(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ uint32_t phandle,
    _Out_ struct FdtNode* node);

/**
 * @brief Find a property by name and return its bytes without copying them.
 *
 * @param node Node to search.
 * @param name Exact property name, such as "reg".
 * @param length Receives the property's size in bytes, or zero if absent.
 * @return Pointer into the original firmware data, or NULL if absent. An empty
 *         property has a non-NULL pointer and a length of zero.
 */
const uint8_t*
FdtProperty(
    _In_ const struct FdtNode* node,
    _In_ const char* name,
    _Out_ uint32_t* length);

/**
 * @brief Check whether the node's "compatible" list contains an exact name.
 *
 * @param node Node to check.
 * @param compatible Hardware name to match, such as "arm,gic-v3".
 * @return 1 for a match, or 0 if absent or the checked data is malformed.
 */
int
FdtCompatible(
    _In_ const struct FdtNode* node,
    _In_ const char* compatible);

/**
 * @brief Read a property containing one 32-bit number.
 *
 * @param node Node containing the property.
 * @param name Property name.
 * @param value Receives the number in the CPU's byte order on success.
 * @return OS_EOK on success, OS_ENOENT if absent, or OS_EINVALPARAMS if the
 *         property is not exactly four bytes long.
 */
oserr_t
FdtScalar(
    _In_ const struct FdtNode* node,
    _In_ const char* name,
    _Out_ uint32_t* value);

#endif
