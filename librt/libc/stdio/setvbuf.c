/**
 * Copyright 2022, Philip Meulengracht
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

#include "errno.h"
#include "internal/_io.h"
#include "internal/_file.h"
#include "stdlib.h"
#include "stdio.h"

static inline void
__flush_existing(FILE* stream)
{
    if (__FILE_IsBuffered(stream) && __FILE_ShouldFlush(stream)) {
        fflush(stream);
    }

    if (stream->Flags & _IOMYBUF) {
        free(stream->_base);
    }

    // reset buffer metrics
    stream->_base = NULL;
    stream->_ptr = NULL;
    stream->_bufsiz = 0;
    stream->_cnt = 0;
}

int setvbuf(
    _In_ FILE*  file,
    _In_ char*  buf,
    _In_ int    mode,
    _In_ size_t size)
{
    if (file == NULL) {
        _set_errno(EINVAL);
        return -1;
    }

    // Assert that the mode passed is valid
    if (mode != _IONBF && mode != _IOFBF && mode != _IOLBF) {
        _set_errno(EINVAL);
        return -1;
    }

    flockfile(file);
    __flush_existing(file);

    file->Flags &= ~(_IOMYBUF | _IOUSRBUF);
    file->BufferMode = mode;

    // If the stream is being set to buffered, and the
    // user is supplying their own buffer, then we use that.
    // Otherwise we set it back to system defaults.
    if (mode != _IONBF && buf != NULL) {
        // user provided us a buffer
        file->Flags  |= _IOUSRBUF;
        file->_base   = file->_ptr = buf;
        file->_bufsiz = (int)size;
    } else {
        // no buffer provided, allocate a new
        io_buffer_allocate(file);
    }
    funlockfile(file);
    return 0;
}
