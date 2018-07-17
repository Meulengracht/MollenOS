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
	// Variables
	StdioObject_t *object   = NULL;
	int result              = -1;

	// Retrieve handle
	object = get_ioinfo(fd);
	if (object == NULL) {
		return result;
	}

    // File or pipe?
    if (object->handle.InheritationType == STDIO_HANDLE_FILE) {
        if (object->exflag & EF_CLOSE) {
            result = (int)CloseFile(object->handle.InheritationData.FileHandle);
            if (_fval(result)) {
                result = -1;
            }
        }
        else {
            result = 0;
        }
    }
    else if (object->handle.InheritationType == STDIO_HANDLE_PIPE) {
        // if it has read caps then we have an issue, close pipe
        if (object->exflag & EF_CLOSE) {
            ClosePipe(object->handle.InheritationData.Pipe.Port);
        }
        else {
            result = 0;
        }
    }

    // Cleanup and return
    StdioFdFree(fd);
    return result;
}

/* fclose
 * Closes a file handle and frees resources associated */
int fclose(FILE *stream)
{
	// Variables
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
