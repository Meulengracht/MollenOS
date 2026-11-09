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
 */

#ifndef __OS_TYPES_GRANTS_H__
#define __OS_TYPES_GRANTS_H__

#include <os/osdefs.h>

#define GRANT_TOKEN_VERSION 2

// Public resource types for grant tokens. The values must not change, and they are
// separate from the kernel handle types.
typedef enum GrantTokenResourceType {
    GrantTokenResourceTypeInvalid = 0,
    // SHM: used for both ordinary buffers and restricted SHM views.
    GrantTokenResourceTypeSHM = 1
} GrantTokenResourceType_t;

typedef enum GrantTokenTargetType {
    // Any process with the token may access the resource.
    GrantTokenTargetTypeAny,
    // Only the security group identified by TargetGlobalHandleID may use the token.
    GrantTokenTargetTypeSpecific
} GrantTokenTargetType_t;

// Access allowed by the token. Each resource type defines which flags it supports.
typedef enum GrantTokenScope {
    // SHM: allows reading the buffer range of the token.
    GrantTokenScopeRead = 0x1,
    // SHM: allows writing the buffer range of the token.
    GrantTokenScopeWrite = 0x2
} GrantTokenScope_t;

/**
 * @brief Export limits for the resource, selected by ResourceType.
 * SHM uses a byte offset and a nonzero length. If a resource type does not use a
 * word, that word must be zero.
 */
typedef union GrantTokenData {
    struct {
        uint32_t Offset;
        uint32_t Length;
    } SHM;

    uint32_t Words[2];
} GrantTokenData_t;

/**
 * @brief Resource type, allowed receiver and export limits of a token.
 * With GrantTokenTargetTypeAny, TargetGlobalHandleID must be UUID_INVALID.
 * With GrantTokenTargetTypeSpecific, it must be the security group of the receiver.
 * ResourceType decides the meaning of Scope and Data. Only SHM is supported right
 * now, with Read, Write or both, and a nonempty byte range.
 */
typedef struct GrantTokenOptions {
    GrantTokenResourceType_t ResourceType;
    GrantTokenTargetType_t   TargetType;
    uuid_t                   TargetGlobalHandleID;
    GrantTokenScope_t        Scope;

    GrantTokenData_t         Data;
} GrantTokenOptions_t;

/**
 * @brief Holds the export ID, the token options and the secret used to verify
 * access to the resource. The creator uses ControlHandleID to revoke the export.
 * ExportGlobalHandleID identifies the kernel record checked during verification.
 */
typedef struct GrantToken {
    uuid_t              ControlHandleID;
    uint32_t            Version;
    uuid_t              ExportGlobalHandleID;
    GrantTokenOptions_t Options;
    uint8_t             Secret[16];
} GrantToken_t;

typedef enum SHMTransferRangeMode {
    // Use the entire token range. RelativeOffset and RequestedLength must be zero.
    SHMTransferRangeFull = 0,
    // Use a nonempty subrange relative to the start of the token range.
    SHMTransferRangeSubrange = 1
} SHMTransferRangeMode_t;

/**
 * @brief A token and the range requested for one SHM operation.
 * ResourceType must be GrantTokenResourceTypeSHM. In subrange mode, RequestedLength
 * must be nonzero, and the range starting at RelativeOffset must fit within
 * Token.Options.Data.SHM.Length. The requested range does not change the range or
 * access flags of the token. Convert this structure to the Gracht contract type
 * before sending it over IPC.
 */
typedef struct SHMTransfer {
    GrantToken_t           Token;
    SHMTransferRangeMode_t RangeMode;
    uint32_t               RelativeOffset;
    uint32_t               RequestedLength;
} SHMTransfer_t;

#endif //!__OS_TYPES_GRANTS_H__
