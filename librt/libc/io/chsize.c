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

#include "io.h"
#include "stdio.h"
#include "errno.h"
#include "internal/_io.h"

int chsize(
    _In_ int  fd,
    _In_ long size)
{
    stdio_handle_t* handle = stdio_handle_get(fd);
    oserr_t         oserr;

    if (handle == NULL) {
        errno = EBADFD;
        return -1;
    }

    if (handle->Ops->resize) {
        oserr = handle->Ops->resize(handle, size);
        if (oserr != OS_EOK) {
            return OsErrToErrNo(oserr);
        }
    } else {
        errno = ENOTSUP;
        return -1;
    }
	
	// clear out eof after resizes
	handle->XTFlags &= ~(WX_ATEOF|WX_READEOF);
	return EOK;
}
