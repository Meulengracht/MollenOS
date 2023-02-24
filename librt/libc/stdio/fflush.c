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

#include "stdio.h"
#include "internal/_io.h"
#include "internal/_file.h"

int fflush(
	_In_ FILE* file)
{
	oserr_t oserr = OS_EOK;

	// If fflush is called with NULL argument
	// we need to flush all buffers present
	if (!file) {
        return io_buffer_flush_all(_IOWRT);
	}

    flockfile(file);
    if (file->_flag & _IOWRT) {
        oserr = io_buffer_flush(file);
		/* @todo
        if(!res && (file->_flag & _IOCOMMIT))
            res = _commit(file->_file) ? EOF : 0; */
	} else if (file->_flag & _IOREAD) {
        // Flushing read files is just resetting the buffer pointer
		file->_cnt = 0;
		file->_ptr = file->_base;
	}
    funlockfile(file);
	return OsErrToErrNo(oserr);
}
