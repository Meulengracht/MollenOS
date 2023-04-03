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

#include <stdio.h>
#include <internal/_io.h>
#include <internal/_file.h>
#include <os/mollenos.h>

int fflush(
	_In_ FILE* file)
{
	oserr_t oserr = OS_EOK;

	// If fflush is called with NULL argument
	// we need to flush all buffers present
	if (!file) {
        io_buffer_flush_all(_IOMOD);
        return 0;
	}

    flockfile(file);
    if (file->Flags & _IOMOD) {
        oserr = io_buffer_flush(file);
    } else {
		file->BytesAvailable = 0;
		file->Current = file->Base;
	}
    funlockfile(file);
	return OsErrToErrNo(oserr);
}
