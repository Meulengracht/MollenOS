/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - C Standard Library
 * - Returns the character currently pointed by the internal file position 
 *   indicator of the specified stream. The internal file position indicator 
 *   is then advanced to the next character.
 */

#include <io.h>
#include <stdio.h>
#include "local.h"

/* _filbuf
 * Fills the buffer attached to the file object and returns a single
 * byte of input. */
int _filbuf(
	_In_ FILE *file)
{
	// Variables
	unsigned char c;

	// Lock access
	_lock_file(file);

	// We can't fill the io buffer if this is a string resource
	if (file->_flag & _IOSTRG) {
		_unlock_file(file);
		return EOF;
	}

	// Allocate buffer if needed
	if (!(file->_flag & (_IONBF | _IOMYBUF | _USERBUF))) {
		os_alloc_buffer(file);
	}

	// If we don't have read access, then make sure we atleast
	// have IORW
	if (!(file->_flag & _IOREAD)) {
		if (file->_flag & _IORW) {
			file->_flag |= _IOREAD;
		}
		else {
			_unlock_file(file);
			return EOF;
		}
	}

	// Is this even a buffered input? If not only read one
	// byte at the time
	if (!(file->_flag & (_IOMYBUF | _USERBUF))) {
		int r;
		
		// Read a single byte
		if ((r = _read(file->_fd, &c, 1)) != 1) {
			file->_flag |= (r == 0) ? _IOEOF : _IOERR;
			_unlock_file(file);
			return EOF;
		}

		// Unlock and return
		_unlock_file(file);
		return c;
	}
	else {
		// Now actually fill the buffer
		file->_cnt = _read(file->_fd, file->_base, file->_bufsiz);

		// If it failed, we are either at end of file or encounted
		// a real error
		if (file->_cnt <= 0) {
			file->_flag |= (file->_cnt == 0) ? _IOEOF : _IOERR;
			file->_cnt = 0;
			
			// Unlock and return
			_unlock_file(file);
			return EOF;
		}

		// Reduce count and return top of buffer
		file->_cnt--;
		file->_ptr = file->_base + 1;
		c = *(unsigned char *)file->_base;

		// Unlock and return
		_unlock_file(file);
		return c;
	}
}

/* fgetc
 * Returns the character currently pointed by the internal file position 
 * indicator of the specified stream. The internal file position indicator 
 * is then advanced to the next character. */
int fgetc(
	_In_ FILE *file)
{
	// Variables
	unsigned char *i;
	unsigned int j;

	// Locked access
	_lock_file(file);

	// Check buffer before filling/raw-reading
	if (file->_cnt > 0) {
		file->_cnt--;
		i = (unsigned char *)file->_ptr++;
		j = *i;
	}
	else {
		j = _filbuf(file);
	}

	// Unlock and return
	_unlock_file(file);
	return j;
}
