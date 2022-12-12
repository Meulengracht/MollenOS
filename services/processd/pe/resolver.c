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
#include <stdlib.h>

static inline oserr_t
__TestFilePath(
        _In_ mstring_t* path)
{
    OsFileDescriptor_t fileDescriptor;
    char*              pathu8;
    oserr_t            osStatus;

    pathu8 = mstr_u8(path);
    if (pathu8 == NULL) {
        return OS_EOOM;
    }

    osStatus = GetFileInformationFromPath(
            pathu8,
            1,
            &fileDescriptor
    );
    free(pathu8);
    return osStatus;
}

static oserr_t
__GuessBasePath(
        _In_  uuid_t      processHandle,
        _In_  mstring_t*  path,
        _Out_ mstring_t** fullPathOut)
{
    // Check the working directory, if it fails iterate the environment defaults
    Process_t* process = PmGetProcessByHandle(processHandle);
    mstring_t* result;
    int        isApp;
    int        isDll;

    TRACE("__GuessBasePath(process=%u, path=%ms)",
          processHandle, path);

    // Start by testing against the loaders current working directory,
    // however this won't work for the base process
    if (process != NULL) {
        result = mstr_fmt("%ms/%ms", process->working_directory, path);
        if (__TestFilePath(result) == OS_EOK) {
            *fullPathOut = result;
            return OS_EOK;
        }
        mstr_delete(result);
    }

    // At this point we have to run through all PATH values
    // Look at the type of file we are trying to load. .app? .dll?
    // for other types its most likely resource load
    isApp = mstr_find_u8(path, ".run", 0);
    isDll = mstr_find_u8(path, ".dll", 0);
    if (isApp != -1 || isDll != -1) {
        result = mstr_fmt("/shared/bin/%ms", path);
    } else {
        result = mstr_fmt("/system/bin/%ms", path);
    }

    if (__TestFilePath(result) == OS_EOK) {
        *fullPathOut = result;
        return OS_EOK;
    }
    else {
        mstr_delete(result);
        return OS_EUNKNOWN;
    }
}

static mstring_t*
__TestRamdiskPath(
        _In_ const char* basePath,
        _In_ mstring_t*  path)
{
    oserr_t    osStatus;
    mstring_t* temporaryResult;
    TRACE("__TestRamdiskPath(basePath=%s, path=%ms)", basePath, path);

    // create the full path for the ramdisk
    temporaryResult = mstr_fmt("%s/%ms", basePath, path);
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
        _In_  uuid_t      processId,
        _In_  mstring_t*  parentPath,
        _In_  mstring_t*  path,
        _Out_ mstring_t** fullPathOut)
{
    oserr_t    osStatus;
    mstring_t* temporaryResult = path;
    TRACE("__ResolveRelativePath(processId=%u, parentPath=%ms, path=%ms)",
          processId, parentPath, path);

    // Let's test against parent being loaded through the ramdisk
    if (parentPath && mstr_find_u8(parentPath, "/initfs/", 0) != -1) {
        // create the full path for the ramdisk
        temporaryResult = __TestRamdiskPath("/initfs/bin", path);
        if (!temporaryResult) {
            // sometimes additional modules will be loaded (i.e fs modules)
            temporaryResult = __TestRamdiskPath("/initfs/modules", path);
        }

        if (temporaryResult) {
            *fullPathOut = temporaryResult;
            return OS_EOK;
        }

        // restore temporaryResult
        temporaryResult = path;
    }

    osStatus = __GuessBasePath(processId, path, &temporaryResult);
    TRACE("__ResolveRelativePath basePath=%ms", temporaryResult);

    // If we already deduced an absolute path skip the canonicalizing moment
    if (osStatus == OS_EOK && mstr_at(temporaryResult, 0) == U'/') {
        *fullPathOut = temporaryResult;
        return osStatus;
    }
    return OS_ENOENT;
}

oserr_t
PeImplResolveFilePath(
        _In_  uuid_t      processId,
        _In_  mstring_t*  parentPath,
        _In_  mstring_t*  path,
        _Out_ mstring_t** fullPathOut)
{
    oserr_t oserr = OS_EOK;
    ENTRY("ResolveFilePath(processId=%u, path=%ms)", processId, path);

    if (mstr_at(path, 0) != U'/') {
        // If we don't even have an environmental identifier present, we
        // have to get creative and guess away
        oserr = __ResolveRelativePath(processId, parentPath, path, fullPathOut);
    } else {
        // Assume absolute path
        *fullPathOut = mstr_clone(path);
    }

    EXIT("ResolveFilePath");
    return oserr;
}
