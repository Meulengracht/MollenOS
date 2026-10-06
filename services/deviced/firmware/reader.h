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

/** Borrowed identity and properties of one node. Views may be copied, but the
 * immutable firmware mapping must outlive every view and property value. */
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

/** @brief Validate the entire structure before visiting nodes. The stack
 * is temporary; copied node views and property spans borrow only the input blob. */
oserr_t
FdtWalkNodes(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ FdtNodeFn visitor,
    _InOut_ void* context);

/** @brief Resolve a unique enabled provider, including forward references. */
oserr_t
FdtFindNode(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ uint32_t phandle,
    _Out_ struct FdtNode* node);

/** @brief Return a borrowed property span; absent properties return NULL. */
const uint8_t*
FdtProperty(
    _In_ const struct FdtNode* node,
    _In_ const char* name,
    _Out_ uint32_t* length);

/** @brief Match a complete compatible string without controller classification. */
int
FdtCompatible(
    _In_ const struct FdtNode* node,
    _In_ const char* compatible);

/** @brief Read a scalar, retaining malformed versus absent property errors. */
oserr_t
FdtScalar(
    _In_ const struct FdtNode* node,
    _In_ const char* name,
    _Out_ uint32_t* value);

#endif
