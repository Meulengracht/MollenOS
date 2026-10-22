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
 * Capabilities Interface
 * - Defines the capabilities that can be granted to processes. Mostly this
 *   applies to privileged operations within the kernel, that drivers or 
 *   services may require.
 */

#ifndef __DDK_CAPABILITIES_H__
#define __DDK_CAPABILITIES_H__

#include <ddk/types/capabilities.h>
#include <ddk/ddkdefs.h>

/**
 * @brief Grants the specified capabilities to the memory space identified by the handle.
 * 
 * @return OS_EOK on success or an appropriate error code on failure.
 */
DDKDECL(oserr_t,
CapabilityGrant(
    _In_ Capabilities_t capabilities,
    _In_ uuid_t         memorySpaceHandle));

/**
 * @brief Parses a capability string and returns the corresponding Capabilities_t value.
 * 
 * Returns 0 if the capability string is invalid, otherwise returns the corresponding Capabilities_t value.
 */
DDKDECL(Capabilities_t,
CapabilityParse(
    _In_ const char* capabilityString));

#endif //!__DDK_CAPABILITIES_H__
