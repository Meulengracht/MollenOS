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

#ifndef __VALI_CAPABILITIES_H__
#define __VALI_CAPABILITIES_H__

#include <os/osdefs.h>
#include <ddk/types/capabilities.h>

/**
 * @brief Grants a specific capability to a memory space. This can only be
 * invoked by processes that hold the CAPABILITY_GRANT_CAPABILITY capability, or
 * from a kernel context.
 */
KERNELABI oserr_t KERNELAPI
CapabilityGrant(
    _In_ Capabilities_t capabilities,
    _In_ uuid_t         memorySpaceHandle);

/**
 * @brief Verifies if the current process holds a specific capability.
 * 
 * @param capabilities The capability to verify.
 * @return OS_EOK if the current process holds the specified capability, otherwise OS_EPERM.
 */
KERNELABI oserr_t KERNELAPI
CapabilityVerify(
    _In_ Capabilities_t capabilities);

#endif //!__VALI_CAPABILITIES_H__
