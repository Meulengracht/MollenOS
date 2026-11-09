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
 * @brief Reads sectors from the filesystem's storage into a shared buffer.
 *
 * @param storageParameters Device or file used for storage.
 * @param buffer Shared buffer ID for the read data.
 * @param offset Byte offset into the shared buffer.
 * @param sector First sector to read.
 * @param count Number of sectors to read.
 * @param read Number of sectors read.
 * @return OS_EOK on success, or an error from the storage request.
 */
extern oserr_t
FSStorageRead(
        _In_  struct VFSStorageParameters* storageParameters,
        _In_  uuid_t                       buffer,
        _In_  size_t                       offset,
        _In_  UInteger64_t*                sector,
        _In_  size_t                       count,
        _Out_ size_t*                      read);

/**
 * @brief Writes sectors from a shared buffer to the filesystem's storage.
 *
 * @param storageParameters Device or file used for storage.
 * @param buffer Shared buffer ID containing the data to write.
 * @param offset Byte offset into the shared buffer.
 * @param sector First sector to write.
 * @param count Number of sectors to write.
 * @param written Number of sectors written.
 * @return OS_EOK on success, or an error from the storage request.
 */
extern oserr_t
FSStorageWrite(
        _In_  struct VFSStorageParameters* storageParameters,
        _In_  uuid_t                       buffer,
        _In_  size_t                       offset,
        _In_  UInteger64_t*                sector,
        _In_  size_t                       count,
        _Out_ size_t*                      written);

/**
 * @brief Gets the storage details, and stores the sector size in
 * storageParameters so later reads and writes can use it.
 *
 * @param storageParameters Device or file used for storage.
 * @param stat Receives the storage descriptor on success.
 * @return OS_EOK on success, or an error from the storage request.
 */
extern oserr_t
FSStorageStat(
        _InOut_ struct VFSStorageParameters* storageParameters,
        _Out_   StorageDescriptor_t*         stat);

#endif //!__FS_COMMON_H__
