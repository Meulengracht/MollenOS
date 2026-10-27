/**
 * MollenOS
 *
 * Copyright, Philip Meulengracht
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

#include <string.h>

#include <core/io.h>

oserr_t
DmIoRelease(
    _InOut_ DeviceIo_t* space,
    _InOut_ int*        acquired)
{
    if (*acquired) {
        oserr_t status = ReleaseDeviceIo(space);
        if (status != OS_EOK) {
            return status;
        }
        *acquired = 0;
    }
    return DmIoDestroy(space, 1);
}

oserr_t
DmIoCreateMemory(
    _Out_   DeviceIo_t* io,
    _In_    uint64_t    base,
    _In_    uint64_t    length)
{
    DeviceIo_t resource = { 0 };
    oserr_t    status;

    if (length == 0 || base > UINTPTR_MAX || length > SIZE_MAX) {
        return OS_ENOTSUPPORTED;
    }
    if (length - 1 > UINTPTR_MAX - base) {
        return OS_ENOTSUPPORTED;
    }

    // The DDK fills the description even when registration fails. Only copy
    // a successful registration into the owner's persistent slot.
    status = CreateDeviceMemoryIo(&resource, (uintptr_t)base, (size_t)length);
    if (status != OS_EOK) {
        return status;
    }

    *io = resource;
    return OS_EOK;
}

oserr_t
DmIoDestroy(
    _InOut_ DeviceIo_t* spaces,
    _In_    size_t      count)
{
    for (size_t i = 0; i < count; i++) {
        oserr_t oserr;
        
        if (spaces[i].Type == DeviceIoInvalid) {
            continue;
        }

        oserr = DestroyDeviceIo(&spaces[i]);
        if (oserr != OS_EOK) {
            return oserr;
        }
        memset(&spaces[i], 0, sizeof(DeviceIo_t));
    }
    return OS_EOK;
}
