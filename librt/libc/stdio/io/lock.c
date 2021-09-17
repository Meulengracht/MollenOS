/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * C Standard Library
 * - Standard IO Support functions
 */

//#define __TRACE
#include <errno.h>
#include <internal/_io.h>
#include <io.h>

int
iolock(
    _In_ int fd)
{
    stdio_handle_t* io = stdio_handle_get(fd);
    if (!io) {
        _set_errno(ENOENT);
        return -1;
    }
    
    if (spinlock_try_acquire(&io->lock) == spinlock_busy) {
        _set_errno(EBUSY);
        return -1;
    }
    return 0;
}

int
iounlock(
    _In_ int fd)
{
    stdio_handle_t* io = stdio_handle_get(fd);
    if (!io) {
        _set_errno(ENOENT);
        return -1;
    }

    spinlock_release(&io->lock);
    return 0;
}
