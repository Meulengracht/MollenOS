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

#ifndef __DDK_INTERNAL_SERVICE_H__
#define __DDK_INTERNAL_SERVICE_H__

#include <gracht/types.h>
#include <os/osdefs.h>

/**
 * @brief Checks whether the transport supplied an authenticated process identity.
 * Services call this before using caller->principal to authorize a request.
 * @param caller Request credentials, or NULL for an unauthenticated transport.
 * @return True for a nonzero kernel-authenticated principal.
 */
static inline bool
__IsAuthenticatedCaller(
    _In_ const struct gracht_security_context* caller)
{
    return caller != NULL &&
           caller->source == GRACHT_SECURITY_KERNEL &&
           caller->principal != 0;
}

#endif //!__DDK_INTERNAL_SERVICE_H__
