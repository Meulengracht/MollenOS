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
 * MollenOS C Library - Reopen file-handle
 */

/* Includes
 * - System */
#include <os/driver/file.h>
#include <os/syscall.h>

/* Includes 
 * - Library */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* Externs */
__EXTERN int _finv(FILE * stream);

/* The freopen
 * Reuses the file
 * handle to either open a new 
 * file or switch access mode */
FILE *freopen(const char * filename, const char * mode, FILE * stream)
{
	/* Sanity input */
	if ((filename == NULL
		&& mode == NULL)
		|| stream == NULL
		|| stream == stdin
		|| stream == stdout
		|| stream == stderr) {
		_set_errno(EINVAL);
		return NULL;
	}

	/* Invalidate the stream buffer */
	_finv(stream);

	/* Ok, if filename is not null 
	 * we must open a new file */
	if (filename != NULL) 
	{
		/* Close existing handle */
		if (_fval((int)CloseFile((stream->fd)))) {
			free(stream);
			return NULL;
		}

		/* Get flags */
		if (mode != NULL) {
			stream->access = faccess(mode);
			stream->opts = fopts(mode);
		}

		/* Re-open handle with new mode-settings */
		if (OpenFile(filename, stream->opts, stream->access, &stream->fd) != FsOk) {
			free(stream);
			return NULL;
		}
	}
	else
	{
		/* Get flags */
		if (mode != NULL) {
			stream->access = faccess(mode);
			stream->opts = fopts(mode);

			/* Update them */
			if (SetFileOptions(stream->fd, stream->opts, stream->access) != OsNoError) {
				_fval((int)CloseFile((stream->fd)));
				free(stream);
				return NULL;
			}
		}
	}

	/* Reset codes */
	stream->code = _IOREAD | _IOFBF;
	_set_errno(EOK);

	/* Done */
	return stream;
}
