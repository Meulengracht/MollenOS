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

#ifndef __OS_SERVICES_PATH_H__
#define __OS_SERVICES_PATH_H__

#include <os/osdefs.h>
#include <os/types/path.h>

_CODE_BEGIN

/**
 * @brief
 * @param path1
 * @param path2
 * @return
 */
CRTDECL(char*,
OSPathJoin(
        _In_ const char* path1,
        _In_ const char* path2));

/**
 * @brief Resolves the full path of the relative/incomplete path provided.
 *
 * @param[In] path The path that should be resolved into an absolute path.
 * @param[In] followSymlinks Whether links should be followed to the true path.
 * @param[In] buffer The buffer where the final path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsNotExists if the path could not be resolved.
 *         OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
OSGetFullPath(
        _In_ const char* path,
        _In_ int         followLinks,
        _In_ char*       buffer,
        _In_ size_t      maxLength));

/**
 * @brief Changes the current working directory. Validation of the target path will be done
 * as a part of this call.
 *
 * @param[In] path The relative or absolute path that should be the new working directory.
 * @return OsInvalidParameters if the parameters passed were not valid.
 *         OsNotExists if the path could not be resolved
 *         OsPathIsNotDirectory If the path is not a directory
 */
CRTDECL(oserr_t,
OSChangeWorkingDirectory(
        _In_ const char *path));

/**
 * @brief Retrieves the current working directory.
 *
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
OSGetWorkingDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
OSGetAssemblyDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
OSGetUserDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
OSGetUserCacheDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
OSGetApplicationDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
OSGetApplicationTemporaryDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

_CODE_END
#endif //!__OS_SERVICES_PATH_H__
