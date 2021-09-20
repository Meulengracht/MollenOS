/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* Standard C Library
*   - Implementation of flush functionality for io-stream.s
*/

#include <stdio.h>
#include <internal/_io.h>

int fflush(
	_In_ FILE *file)
{
	OsStatus_t Result = OsSuccess;

	// If fflush is called with NULL argument
	// we need to flush all buffers present
	if (!file) {
        io_buffer_flush_all(_IOWRT);
	}
	else if (file->_flag & _IOWRT) {
		_lock_stream(file);
		Result = io_buffer_flush(file);
		/* @todo
        if(!res && (file->_flag & _IOCOMMIT))
            res = _commit(file->_file) ? EOF : 0; */
		_unlock_stream(file);
		return (Result == OsSuccess) ? 0 : 1;
	}
	// Flushing read files is just resetting the buffer pointer
	else if (file->_flag & _IOREAD) {
		_lock_stream(file);
		file->_cnt = 0;
		file->_ptr = file->_base;
		_unlock_stream(file);
	}
	return 0;
}
