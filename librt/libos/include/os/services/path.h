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
GetFullPath(
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
ChangeWorkingDirectory(
        _In_ const char *path));

/**
 * @brief Retrieves the current working directory.
 *
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
GetWorkingDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
GetAssemblyDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
GetUserDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
GetUserCacheDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
GetApplicationDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(oserr_t,
GetApplicationTemporaryDirectory(
        _In_ char*  buffer,
        _In_ size_t maxLength));

_CODE_END
#endif //!__OS_SERVICES_PATH_H__
