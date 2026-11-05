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

#define GRANT_TOKEN_VERSION 1

typedef enum GrantTokenTargetType {
    // Any process with the token may access the resource.
    GrantTokenTargetTypeAny,
    // Only the security group identified by TargetGlobalHandleID may use the token.
    GrantTokenTargetTypeSpecific
} GrantTokenTargetType_t;

// Access allowed by the token. Combine both flags for read/write access.
typedef enum GrantTokenScope {
    // Allow reading the permitted buffer range.
    GrantTokenScopeRead = 0x1,
    // Allow writing the permitted buffer range.
    GrantTokenScopeWrite = 0x2
} GrantTokenScope_t;

/**
 * @brief Specifies the allowed receiver, access and buffer range for a grant token.
 * With GrantTokenTargetTypeAny, TargetGlobalHandleID must be UUID_INVALID.
 * With GrantTokenTargetTypeSpecific, it must identify the receiver's security group.
 * Scope must contain GrantTokenScopeRead, GrantTokenScopeWrite or both.
 * Other bits are invalid.
 */
typedef struct GrantTokenOptions {
    GrantTokenTargetType_t TargetType;
    uuid_t                 TargetGlobalHandleID;
    GrantTokenScope_t      Scope;

    // Byte offset and length within the SHM buffer. Length must be nonzero.
    uint32_t               Offset;
    uint32_t               Length;
} GrantTokenOptions_t;

/**
 * @brief Stores the export ID, options and secret used to verify buffer access.
 * The creator uses ControlHandleID to revoke the export. ExportGlobalHandleID
 * identifies the kernel record used during verification.
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
 * @brief Describes a token and the range requested for one SHM operation.
 * In subrange mode, RequestedLength must be nonzero and the range starting at
 * RelativeOffset must fit within Token.Options.Length. The token's permitted
 * range and access flags stay unchanged. Convert this structure to the Gracht
 * contract type before sending it over IPC.
 */
typedef struct SHMTransfer {
    GrantToken_t           Token;
    SHMTransferRangeMode_t RangeMode;
    uint32_t               RelativeOffset;
    uint32_t               RequestedLength;
} SHMTransfer_t;

#endif //!__OS_TYPES_GRANTS_H__
