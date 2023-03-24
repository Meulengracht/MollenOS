/**
 * Copyright 2017, Philip Meulengracht
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

#include <io.h>
#include <internal/_io.h>
#include <internal/_file.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

/**
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
    stdio_handle_t* handle;
    int             flags;

    if (fd < 0 || mode == NULL) {
        _set_errno(EINVAL);
        return NULL;
    }

    if (__fmode_to_flags(mode, &flags)) {
        return NULL;
    }
    
    handle = stdio_handle_get(fd);
    if (!handle) {
        _set_errno(EBADFD);
        return NULL;
    }
    
    if (stdio_handle_set_buffered(handle, NULL, _IORW, _IOFBF)) {
        return NULL;
    }
    return stdio_handle_stream(handle);
}

FILE *fopen(const char* filename, const char* mode)
{
    int flags;
    int fd;

    if (filename == NULL || mode == NULL) {
        _set_errno(EINVAL);
        return NULL;
    }

    if (__fmode_to_flags(mode, &flags)) {
        return NULL;
    }

    // Open file as file-descriptor
    fd = open(filename, flags, 0755);
    if (fd == -1) {
        return NULL;
    }
    return fdopen(fd, mode);
}
