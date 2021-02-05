/* MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * ResetEvents - Basic synchronization primitive that can be used with
 * wait/set.
 */

#include <event.h>
#include <internal/_io.h>
#include <internal/_syscalls.h>

int eventd(unsigned int initialValue, unsigned int flags)
{
    stdio_handle_t* ioObject;
    int             status;
    UUId_t          handle;
    atomic_int*     syncAddress;

    if (EVT_TYPE(flags) == EVT_RESET_EVENT) {
        initialValue = 1;
    }

    status = stdio_handle_create(-1, WX_OPEN | WX_DONTINHERIT, &ioObject);
    if (status) {
        return status;
    }

    if (Syscall_EventCreate(initialValue, flags, &handle, &syncAddress) != OsSuccess) {
        stdio_handle_destroy(ioObject, 0);
        errno = ENOSYS;
        return -1;
    }

    stdio_handle_set_handle(ioObject, handle);
    stdio_handle_set_ops_type(ioObject, STDIO_HANDLE_EVENT);

    ioObject->object.data.evt.flags   = flags;
    ioObject->object.data.evt.options = 0;
    ioObject->object.data.evt.initialValue = initialValue;
    ioObject->object.data.evt.sync_address = syncAddress;

    return ioObject->fd;
}
