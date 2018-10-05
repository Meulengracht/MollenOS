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
 * - Closes a given file-handle and cleans up
 */

#include <os/file.h>
#include <os/syscall.h>
#include <os/utils.h>

#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "local.h"

/* close
 * This is ANSI C close function and works with filedescriptors */
int close(int fd)
{
	StdioObject_t*  Object;
	int             Result = 0;

	// Retrieve handle
	Object = get_ioinfo(fd);
	if (Object == NULL) {
        _set_errno(EBADFD);
		return -1;
	}
    
    // File or pipe?
    if (Object->handle.InheritationType == STDIO_HANDLE_FILE) {
        if (Object->exflag & EF_CLOSE) {
            Result = (int)CloseFile(Object->handle.InheritationData.FileHandle);
            if (_fval(Result)) {
                Result = -1;
            }
        }
    }
    else if (Object->handle.InheritationType == STDIO_HANDLE_PIPE) {
        // if it has read caps then we have an issue, close pipe
        if (Object->exflag & EF_CLOSE) {
            if (ClosePipe(Object->handle.InheritationData.Pipe.Port) != OsSuccess) {
                Result = -1;
            }
        }
    }
    StdioFdFree(fd);
    return Result;
}

/* fclose
 * Closes a file handle and frees resources associated */
int fclose(FILE *stream)
{
	int r, flag;

    // Flush file before anything
	_lock_file(stream);
	if (stream->_flag & _IOWRT) {
		fflush(stream);
	}
	flag = stream->_flag;

	// Flush and free associated buffers
	if (stream->_tmpfname != NULL) {
		free(stream->_tmpfname);
	}
	if (stream->_flag & _IOMYBUF) {
		free(stream->_base);
	}

	// Call underlying close and never
    // unlock the file as underlying stream is closed
	r = close(stream->_fd);
	free(stream);
	return ((r == -1) || (flag & _IOERR) ? EOF : 0);
}
