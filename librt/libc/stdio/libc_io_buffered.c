/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * C Standard Library
 * - Standard IO Support functions
 */
//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <internal/_io.h>
#include <io.h>
#include <stdlib.h>

/* os_alloc_buffer
 * Allocates a transfer buffer for a stdio file stream */
OsStatus_t
os_alloc_buffer(
    _In_ FILE *file)
{
    // Sanitize that it's not an std tty stream
    if ((file->_fd == STDOUT_FILENO || file->_fd == STDERR_FILENO) && isatty(file->_fd)) {
        return OsError;
    }

    // Allocate a transfer buffer
    file->_base = calloc(1, INTERNAL_BUFSIZ);
    if (file->_base) {
        file->_bufsiz = INTERNAL_BUFSIZ;
        file->_flag |= _IOMYBUF;
    }
    else {
        file->_base = (char *)(&file->_charbuf);
        file->_bufsiz = 2;
        file->_flag |= _IONBF;
    }

    // Update pointer to base and 0 count
    file->_ptr = file->_base;
    file->_cnt = 0;
    return OsSuccess;
}

/* add_std_buffer
 * Allocate temporary buffer for stdout and stderr */
OsStatus_t
add_std_buffer(
    _In_ FILE *file)
{
    // Static write buffers
    static char buffers[2][BUFSIZ];

    // Sanitize the file stream
    if ((file->_fd != STDOUT_FILENO && file->_fd != STDERR_FILENO) 
        || (file->_flag & (_IONBF | _IOMYBUF | _USERBUF)) 
        || !isatty(file->_fd)) {
        return OsError;
    }

    // Update buffer pointers
    file->_ptr = file->_base =
        buffers[file->_fd == STDOUT_FILENO ? 0 : 1];
    file->_bufsiz = file->_cnt = BUFSIZ;
    file->_flag |= _USERBUF;
    return OsSuccess;
}

/* remove_std_buffer
 * Removes temporary buffer from stdout or stderr
 * Only call this function when add_std_buffer returned Success */
void
remove_std_buffer(
    _In_ FILE *file)
{
    os_flush_buffer(file);
    file->_ptr    = file->_base = NULL;
    file->_bufsiz = file->_cnt = 0;
    file->_flag   &= ~_USERBUF;
}

/* os_flush_buffer
 * Flushes the number fo bytes stored in the buffer and resets
 * the buffer to initial state */
OsStatus_t
os_flush_buffer(
    _In_ FILE* file)
{
    if ((file->_flag & (_IOREAD | _IOWRT)) == _IOWRT && 
        file->_flag & (_IOMYBUF | _USERBUF)) {
        int cnt = file->_ptr - file->_base;

        // Flush them
        if (cnt > 0 && write(file->_fd, file->_base, cnt) != cnt) {
            file->_flag |= _IOERR;
            return OsError;
        }

        // If it's rw, clear the write flag
        if (file->_flag & _IORW) {
            file->_flag &= ~_IOWRT;
        }
        file->_ptr = file->_base;
        file->_cnt = 0;
    }
    return OsSuccess;
}

/* os_flush_all_buffers
 * Flush all stream buffer */
int
os_flush_all_buffers(
    _In_ int mask)
{
    stdio_handle_t*  Object;
    int             FilesFlushes = 0;
    FILE*           File;

    LOCK_FILES();
    foreach(Node, stdio_get_handles()) {
        Object  = (stdio_handle_t*)Node->Data;
        File    = Object->buffered_stream;
        if (File != NULL && (File->_flag & mask)) {
            fflush(File);
            FilesFlushes++;
        }
    }
    UNLOCK_FILES();
    return FilesFlushes;
}

OsStatus_t
_lock_file(
    _In_ FILE *file)
{
    TRACE("_lock_file(0x%" PRIxIN ")", file);
    if (!(file->_flag & _IOSTRG)) {
        assert(stdio_handle_get(file->_fd) != NULL);
        spinlock_acquire(&stdio_handle_get(file->_fd)->lock);
    }
    return OsSuccess;
}

OsStatus_t
_unlock_file(
    _In_ FILE *file)
{
    TRACE("_unlock_file(0x%" PRIxIN ")", file);
    if (!(file->_flag & _IOSTRG)) {
        assert(stdio_handle_get(file->_fd) != NULL);
        spinlock_release(&stdio_handle_get(file->_fd)->lock);
    }
    return OsSuccess;
}
