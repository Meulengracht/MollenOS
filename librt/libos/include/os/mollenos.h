/**
 * Copyright 2011, Philip Meulengracht
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
 * OS Interface
 * - This header describes the os-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __MOLLENOS_H__
#define __MOLLENOS_H__

#include <os/osdefs.h>
#include <os/types/query.h>

/* Cache Type Definitions
 * Flags that can be used when requesting a flush of one of the hardware caches */
#define CACHE_INSTRUCTION   1
#define CACHE_MEMORY        2

_CODE_BEGIN

/**
 * @brief0
 * @param code
 * @return
 */
CRTDECL(int,
OsErrToErrNo(
        _In_ oserr_t code));

/**
 * @brief Query system information.
 * @param request
 * @param buffer
 * @param maxSize
 * @param bytesQueriedOut
 * @return
 */
CRTDECL(oserr_t,
OSSystemQuery(
        _In_ enum OSSystemQueryRequest request,
        _In_ void*                     buffer,
        _In_ size_t                    maxSize,
        _In_ size_t*                   bytesQueriedOut));

/**
 * @brief
 * @param Cache
 * @param Start
 * @param Length
 * @return
 */
CRTDECL(oserr_t,
FlushHardwareCache(
        _In_ int    Cache,
        _In_ void*  Start,
        _In_ size_t Length));

_CODE_END
#endif //!__MOLLENOS_H__
