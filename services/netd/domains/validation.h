/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 
#ifndef __NETMANAGER_VALIDATION_H__
#define __NETMANAGER_VALIDATION_H__

#include <inet/local.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief Validate the wire length before inspecting any caller-controlled fields.
 */
static inline oserr_t
ValidateLocalAddress(const void* data, size_t length)
{
    const struct sockaddr_lc* address = data;
    const size_t              prefix  = offsetof(struct sockaddr_lc, slc_addr);

    if (data == NULL || length <= prefix || length > sizeof(*address)) {
        return OS_EINVALPARAMS;
    }
    
    if (address->slc_family != AF_LOCAL || address->slc_len != length) {
        return OS_EINVALPARAMS;
    }
    
    if (address->slc_addr[0] == '\0' ||
        memchr(address->slc_addr, '\0', length - prefix) == NULL) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

#endif //!__NETMANAGER_VALIDATION_H__
