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
 * - Sets the position indicator associated with the stream to a new position.
 */

/* Includes */
#include <stdio.h>
#include "local.h"

/* fsetpos
 * Sets the position indicator associated with the stream to a new position. */
int fsetpos(
	_In_ FILE *stream, 
	_In_ __CONST fpos_t *pos)
{
	// Variables
	int ret;

	// Lock the stream
	_lock_file(stream);

	// If the input stream is buffered we flush it
	if (stream->_flag & _IOWRT) {
		os_flush_buffer(stream);
	}

	// Discard buffered input
	stream->_cnt = 0;
	stream->_ptr = stream->_base;

	// Reset direction of i/o
	if (stream->_flag & _IORW) {
		stream->_flag &= ~(_IOREAD | _IOWRT);
	}

	// Now actually set the position
	ret = (_lseeki64(stream->_fd, *pos, SEEK_SET) == -1) ? -1 : 0;

	// Unlock and return
	_unlock_file(stream);
	return ret;
}
