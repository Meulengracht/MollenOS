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

#include <ddk/utils.h>
#include <ds/mstring.h>
#include <os/services/file.h>
#include <os/services/path.h>
#include <stdlib.h>
#include <string.h>
#include "process.h"
#include "private.h"

static mstring_t*
__TestRamdiskPath(
        _In_ const char* basePath,
        _In_ const char* path)
{
    oserr_t    osStatus;
    mstring_t* temporaryResult;
    TRACE("__TestRamdiskPath(basePath=%s, path=%ms)", basePath, path);

    // create the full path for the ramdisk
    temporaryResult = mstr_fmt("%s/%s", basePath, path);
    if (temporaryResult == NULL) {
        return NULL;
    }

    // try to find the file in the ramdisk
    osStatus = PmBootstrapFindRamdiskFile(temporaryResult, NULL, NULL);
    if (osStatus == OS_EOK) {
        return temporaryResult;
    }
    mstr_delete(temporaryResult);
    return NULL;
}

static oserr_t
__ResolveRelativePath(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  const char*                path,
        _Out_ mstring_t**                fullPathOut)
{
    oserr_t            oserr;
    OsFileDescriptor_t fileDescriptor;
    TRACE("__ResolveRelativePath(processId=%u, parentPath=%ms, path=%ms)",
          processId, parentPath, path);

    for (char* i = strtok(loadContext->Paths, ";"); i; i = strtok(NULL, ";")) {
        char* combined = OSPathJoin(i, path);
        if (strstr(combined, "/initfs/") != NULL) {
            mstring_t* result = __TestRamdiskPath(i, path);
            if (result) {
                *fullPathOut = mstr_new_u8(combined);
                free(combined);
                break;
            }
        } else {
            oserr = GetFileInformationFromPath(
                    combined,
                    1,
                    &fileDescriptor
            );
            if (oserr == OS_EOK) {
                *fullPathOut = mstr_new_u8(combined);
                free(combined);
                break;
            }
        }
        free(combined);
    }
    return OS_EOK;
}

oserr_t
PEResolvePath(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 path,
        _Out_ mstring_t**                fullPathOut)
{
    oserr_t oserr = OS_EOK;
    char*   cpath;
    ENTRY("ResolveFilePath(processId=%u, path=%ms)", processId, path);

    if (mstr_at(path, 0) != U'/') {
        cpath = mstr_u8(path);
        if (cpath == NULL) {
            return OS_EOOM;
        }

        // If we don't even have an environmental identifier present, we
        // have to get creative and guess away
        oserr = __ResolveRelativePath(loadContext, cpath, fullPathOut);
        free(cpath);
    } else {
        // Assume absolute path
        *fullPathOut = mstr_clone(path);
    }

    EXIT("ResolveFilePath");
    return oserr;
}
