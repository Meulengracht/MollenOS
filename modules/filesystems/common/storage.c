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

#include <fs/common.h>

oserr_t
FSStorageRead(
        _In_ struct VFSStorageParameters* storageParameters,
        _In_ uuid_t                       buffer,
        _In_ size_t                       offset,
        _In_ UInteger64_t*                sector,
        _In_ size_t                       count,
        _Out_ size_t*                     read)
{
    return OsNotSupported;
}

oserr_t
FSStorageWrite(
        _In_ struct VFSStorageParameters* storageParameters,
        _In_ uuid_t                       buffer,
        _In_ size_t                       offset,
        _In_ UInteger64_t*                sector,
        _In_ size_t                       count,
        _Out_ size_t*                     written)
{
    return OsNotSupported;
}

void
FSStorageStat(
        _In_ struct VFSStorageParameters* storageParameters,
        _In_ StorageDescriptor_t*         stat)
{

}
