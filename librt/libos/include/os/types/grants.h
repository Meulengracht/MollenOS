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

typedef enum GrantTokenTargetType {
    // Any process with the token may access the resource.
    GrantTokenTargetTypeAny,
    // Only the security group identified by TargetGlobalHandleID may use the token.
    GrantTokenTargetTypeSpecific
} GrantTokenTargetType_t;

// Access allowed by the token. Verification requires an exact scope match.
typedef enum GrantTokenScope {
    GrantTokenScopeRead,
    GrantTokenScopeWrite
} GrantTokenScope_t;

/**
 * @brief Specifies the allowed receiver, access and buffer range for a grant token.
 * With GrantTokenTargetTypeAny, TargetGlobalHandleID must be UUID_INVALID.
 * With GrantTokenTargetTypeSpecific, it must identify the receiver's security group.
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
 * @brief Carries the export ID, options and secret needed to verify buffer access.
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

#endif //!__OS_TYPES_GRANTS_H__
