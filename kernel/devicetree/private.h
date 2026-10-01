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

#ifndef __DEVICETREE_PRIVATE_H__
#define __DEVICETREE_PRIVATE_H__

#include <devicetree.h>

#define __STATIC_FDT_MAX_DEPTH 32

struct __ParserContext {
    oserr_t (*BeginNode)(void* userData, const char* name, uint32_t nameLength);
    oserr_t (*Property)(void* userData, const char* name, const void* value, uint32_t valueLength);
    oserr_t (*EndNode)(void* userData);
    void*    UserData;
};

static inline uint32_t
__ReadBe32(
    _In_ const void* value)
{
    const uint8_t* p = value;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

__EXTERN oserr_t
__ParseFDTHeader(
    _In_  const void*       deviceTree,
    _In_  uint32_t          size,
    _Out_ struct FDTHeader* header);

__EXTERN oserr_t
__ParseStructureBlock(
    _In_ const void*             structureBlock,
    _In_ uint32_t                structureBlockSize,
    _In_ const char*             stringBlock,
    _In_ uint32_t                stringBlockSize,
    _In_ struct __ParserContext* context);

#endif //!__DEVICETREE_PRIVATE_H__
