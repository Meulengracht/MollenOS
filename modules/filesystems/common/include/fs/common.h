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
 *
 */

#ifndef __FS_COMMON_H__
#define __FS_COMMON_H__

#include <ddk/filesystem.h>

/**
 * @brief
 * @param storageParameters
 * @param buffer
 * @param offset
 * @param sector
 * @param count
 * @param read
 * @return
 */
static oserr_t
FSStorageRead(
        _In_ struct VFSStorageParameters* storageParameters,
        _In_ uuid_t                       buffer,
        _In_ size_t                       offset,
        _In_ UInteger64_t*                sector,
        _In_ size_t                       count,
        _Out_ size_t*                     read);

/**
 * @brief
 * @param storageParameters
 * @param buffer
 * @param offset
 * @param sector
 * @param count
 * @param written
 * @return
 */
static oserr_t
FSStorageWrite(
        _In_ struct VFSStorageParameters* storageParameters,
        _In_ uuid_t                       buffer,
        _In_ size_t                       offset,
        _In_ UInteger64_t*                sector,
        _In_ size_t                       count,
        _Out_ size_t*                     written);

/**
 * @brief
 * @param storageParameters
 * @param stat
 */
extern void
FSStorageStat(
        _In_ struct VFSStorageParameters* storageParameters,
        _In_ StorageDescriptor_t*         stat);

#endif //!__FS_COMMON_H__
