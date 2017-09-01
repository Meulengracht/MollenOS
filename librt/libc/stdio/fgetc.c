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
* MollenOS C Library - File Get Character
*/

#include <stdio.h>
#include "local.h"

int _filbuf(
	_In_ FILE *file)
{
	unsigned char c;
	_lock_file(file);

	if (file->_flag & _IOSTRG) {
		_unlock_file(file);
		return EOF;
	}

	/* Allocate buffer if needed */
	if (!(file->_flag & (_IONBF | _IOMYBUF | _USERBUF)))
		os_alloc_buffer(file);

	if (!(file->_flag & _IOREAD))
	{
		if (file->_flag & _IORW)
			file->_flag |= _IOREAD;
		else
		{
			_unlock_file(file);
			return EOF;
		}
	}

	if (!(file->_flag & (_IOMYBUF | _USERBUF)))
	{
		int r;
		if ((r = read_i(file->_fd, &c, 1)) != 1) {
			file->_flag |= (r == 0) ? _IOEOF : _IOERR;
			_unlock_file(file);
			return EOF;
		}

		_unlock_file(file);
		return c;
	}
	else
	{
		file->_cnt = read_i(file->_fd, file->_base, file->_bufsiz);
		if (file->_cnt <= 0)
		{
			file->_flag |= (file->_cnt == 0) ? _IOEOF : _IOERR;
			file->_cnt = 0;
			_unlock_file(file);
			return EOF;
		}

		file->_cnt--;
		file->_ptr = file->_base + 1;
		c = *(unsigned char *)file->_base;
		_unlock_file(file);
		return c;
	}
}

int fgetc(
	_In_ FILE *file)
{
	unsigned char *i;
	unsigned int j;

	_lock_file(file);
	if (file->_cnt > 0) {
		file->_cnt--;
		i = (unsigned char *)file->_ptr++;
		j = *i;
	}
	else {
		j = _filbuf(file);
	}

	_unlock_file(file);
	return j;
}
