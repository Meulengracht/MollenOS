/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __STDIO_PRIVATE_H__
#define __STDIO_PRIVATE_H__

#include <ds/hashtable.h>
#include <internal/_io.h>
#include <os/types/grants.h>

/**
 * @brief Describes one selected descriptor's token and transferable mode flags.
 * The caller must keep the descriptor and attachment unchanged while preparing
 * the snapshot. This function creates no token and adds no references.
 */
oserr_t
CRTPrepareInheritedHandle(
    _In_  const stdio_handle_t* handle,
    _Out_ unsigned int*         flagsOut,
    _Out_ GrantTokenOptions_t*  optionsOut);

/**
 * @brief Checks that a descriptor type is supported and that its mode flags are
 * allowed for that type. A type is supported when it is in the inheritance table.
 * Token scope is not checked here. The kernel limits access through the
 * installed reference.
 */
oserr_t
CRTValidateInheritedDescriptor(
    _In_ unsigned int signature,
    _In_ unsigned int flags);

/**
 * @brief Reconstructs one validated resource from an installed child reference.
 * The caller attaches aliases only after success. A failure requires startup
 * termination to release references not registered locally.
 */
oserr_t
CRTAdoptInheritedHandle(
    _In_  unsigned int signature,
    _In_  uuid_t       reference,
    _Out_ OSHandle_t*  handle);

/**
 * @brief Adds intrinsic stream flags to previously validated descriptor modes.
 */
unsigned int
CRTInheritedLocalFlags(
    _In_ unsigned int signature,
    _In_ unsigned int flags);

/**
 * @brief Retrieves the hashtable of IO descriptors.
 * @return A pointer to the hashtable of io-descriptors.
 */
extern hashtable_t*
IODescriptors(void);

/**
 * @brief Converts a signature to their respective implementation
 * operations.
 * @param signature The signature of the stdio operations.
 * @return A pointer to operations.
 */
stdio_ops_t*
CRTSignatureOps(
        _In_ unsigned int signature);

#endif //!__STDIO_PRIVATE_H__
