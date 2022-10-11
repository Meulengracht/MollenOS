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

typedef struct SystemDescriptor {
    size_t NumberOfProcessors;
    size_t NumberOfActiveCores;

    size_t PagesTotal;
    size_t PagesUsed;
    size_t PageSizeBytes;
    size_t AllocationGranularityBytes;
} SystemDescriptor_t;

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
 * @brief
 * @param descriptor
 * @return
 */
CRTDECL(oserr_t,
SystemQuery(
        _In_ SystemDescriptor_t* descriptor));

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
