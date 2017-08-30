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
* MollenOS C Library - File Flush
*/

#include <stdio.h>

int fflush(
	_In_ FILE *file)
{
	if (!file)
	{
		os_flush_all_buffers(_IOWRT);
	}
	else if (file->_flag & _IOWRT)
	{
		int res;

		_lock_file(file);
		res = os_flush_buffer(file);
		/* FIXME
        if(!res && (file->_flag & _IOCOMMIT))
            res = _commit(file->_file) ? EOF : 0;
        */
		_unlock_file(file);

		return res;
	}
	else if (file->_flag & _IOREAD)
	{
		_lock_file(file);
		file->_cnt = 0;
		file->_ptr = file->_base;
		_unlock_file(file);

		return 0;
	}
	return 0;
}
