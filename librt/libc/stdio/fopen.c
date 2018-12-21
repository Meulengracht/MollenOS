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
 * MollenOS C Library - File Opening & File Creation
 */

#include <os/ipc/pipe.h>
#include <os/utils.h>
#include <os/file.h>

#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "local.h"

/* split_oflags
 * Generates WX flags from the stdc opening flags */
unsigned split_oflags(unsigned oflags)
{
    int         wxflags = 0;
    unsigned    unsupp; // until we support everything

    if (oflags & O_APPEND)               wxflags |= WX_APPEND;
    if (oflags & O_BINARY)               {/* Nothing to do */}
    else if (oflags & O_TEXT)            wxflags |= WX_TEXT;
    else if (oflags & O_WTEXT)           wxflags |= WX_TEXT;
    else if (oflags & O_U16TEXT)         wxflags |= WX_TEXT;
    else if (oflags & O_U8TEXT)          wxflags |= WX_TEXT;
    else                                 wxflags |= WX_TEXT; // default to TEXT
    if (oflags & O_NOINHERIT)            wxflags |= WX_DONTINHERIT;

    if ((unsupp = oflags & ~(
                    O_BINARY|O_TEXT|O_APPEND|
                    O_TRUNC|O_EXCL|O_CREAT|
                    O_RDWR|O_WRONLY|O_TEMPORARY|
                    O_NOINHERIT|
                    O_SEQUENTIAL|O_RANDOM|O_SHORT_LIVED|
                    O_WTEXT|O_U16TEXT|O_U8TEXT
                    ))) {
        TRACE(":unsupported oflags 0x%x\n", unsupp);
    }
    return wxflags;
}

/* open
 * ANSII Version of the fopen. Handles flags and creation flags. */
int open(const char* file, int flags, ...)
{
    StdioObject_t*      Object;
    UUId_t              Handle;
    FileSystemCode_t    Code;
    int                 wxflags;
    int                 pmode = 0;
    int                 fd = -1;
    va_list             ap;

    if (file == NULL) {
        _set_errno(EINVAL);
        return -1;
    }

    // Extract pmode flags
    if (flags & O_CREAT) {
        va_start(ap, flags);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    wxflags = split_oflags((unsigned int)flags);

    // Invoke os service
    Code = OpenFile(file, _fopts(flags), _faccess(flags), &Handle);
    if (!_fval(Code)) {
        fd = StdioFdAllocate(-1, wxflags);
        if (fd != -1) {
            Object = get_ioinfo(fd);
            StdioCreateFileHandle(Handle, Object);
            if (flags & O_WTEXT) {
                Object->exflag |= EF_UTF16|EF_UNK_UNICODE;        
            }
            else if (flags & O_U16TEXT) {
                Object->exflag |= EF_UTF16;
            }
            else if (flags & O_U8TEXT) {
                Object->exflag |= EF_UTF8;
            }
        }
        else {
            _set_errno(ENOENT); 
            CloseFile(Handle);
        }
    }
    else {
        _set_errno(EACCES);
        CloseFile(Handle);
    }
    return fd;
}

/* pipe
 * Create a pipe by using a file-descriptor as a handle. These pipes are then inherited
 * by sub-processes. */
int pipe(void)
{
    OsStatus_t  Status;
    int         fd;

    // Allocate a file descriptor first, but since the lower numbers of pipes
    // usually are reserved, we add 64 as a base
    fd = StdioFdAllocate(-1, WX_PIPE);
    if (fd == -1) {
        _set_errno(ENOENT);
        return -1;
    }

    Status = StdioCreatePipeHandle(UUID_INVALID, get_ioinfo(fd));
    if (Status == OsError) {
        StdioFdFree(fd);
        _set_errno(ENOENT);
        return -1;
    }
    return fd;
}

/* Information 
"r"    read: Open file for input operations. The file must exist.
"w"    write: Create an empty file for output operations. If a file with the same name already exists, its contents are discarded and the file is treated as a new empty file.
"a"    append: Open file for output at the end of a file. Output operations always write data at the end of the file, expanding it. 
    Repositioning operations (fseek, fsetpos, rewind) are ignored. The file is created if it does not exist.
"r+" read/update: Open a file for update (both for input and output). The file must exist.
"w+" write/update: Create an empty file and open it for update (both for input and output). If a file with the same name already exists its contents are discarded and the file is treated as a new empty file.
"a+" append/update: Open a file for update (both for input and output) with all output operations writing data at the end of the file. 
     Repositioning operations (fseek, fsetpos, rewind) affects the next input operations, but output operations move the position back to the end of file. The file is created if it does not exist.
*/
FILE* fdopen(int fd, const char *mode)
{
    int open_flags, stream_flags;
    FILE *stream;

    if (fd < 0 || mode == NULL) {
        _set_errno(EINVAL);
        return NULL;
    }
    _fflags(mode, &open_flags, &stream_flags);

    // Allocate a new file instance and reset the structure
    stream = (FILE*)malloc(sizeof(FILE));
    memset(stream, 0, sizeof(FILE));
    
    // Initialize a handle
    if (StdioFdInitialize(stream, fd, stream_flags) != OsSuccess) {
        free(stream);
        return NULL;
    }
    return stream;
}

/* fopen
 * Opens the file whose name is specified in the parameter filename 
 * and associates it with a stream that can be identified in future 
 * operations by the FILE pointer returned. */
FILE *fopen(const char* filename, const char* mode)
{
    int open_flags, stream_flags;
    int fd = 0;

    if (filename == NULL || mode == NULL) {
        _set_errno(EINVAL);
        return NULL;
    }
    _fflags(mode, &open_flags, &stream_flags);

    // Open file as file-descriptor
    fd = open(filename, open_flags, S_IREAD | S_IWRITE);
    if (fd == -1) {
        return NULL;
    }
    return fdopen(fd, mode);
}
