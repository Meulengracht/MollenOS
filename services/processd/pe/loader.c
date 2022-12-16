/**
 * Copyright 2022, Philip Meulengracht
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

#define __TRACE
#include <ddk/utils.h>
#include "private.h"
#include "pe.h"

static oserr_t
__PostProcessImage(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ struct ModuleMapping*      moduleMapping)
{
    oserr_t oserr;
    TRACE("__PostProcessImage()");

    oserr = PEImportsProcess(loadContext, moduleMapping);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = PERuntimeRelocationsProcess(moduleMapping);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return OS_EOK;
}

oserr_t
PEImageLoad(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 path)
{
    struct ModuleMapping* moduleMapping;
    mstring_t*            resolvedPath;
    oserr_t               oserr;
    TRACE("PEImageLoad(path=%ms)", path);

    oserr = PEResolvePath(
            loadContext,
            path,
            &resolvedPath
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = MapperLoadModule(
            loadContext,
            resolvedPath,
            &moduleMapping
    );
    mstr_delete(resolvedPath);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __PostProcessImage(loadContext, moduleMapping);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return OS_EOK;
}
