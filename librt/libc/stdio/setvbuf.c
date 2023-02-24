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

static inline void __flush_existing(FILE* stream)
{
    if (!(stream->_flag & _IONBF)) {
        fflush(stream);

        // cleanup any stdio malloced buffer
        if (stream->_flag & _IOMYBUF) {
            free(stream->_base);
        }
    }
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

    // no-buffering we only support the static storage of up to 1 byte
    if (mode == _IONBF && size >= 2) {
        _set_errno(EINVAL);
        return -1;
    }

    flockfile(file);
    __flush_existing(file);

    file->_flag &= ~(_IONBF | _IOMYBUF | _USERBUF | _IOLBF);
    file->_flag |= mode;
    file->_cnt   = 0;

    if (buf) {
        // user provided us a buffer
        file->_flag  |= _USERBUF;
        file->_base   = file->_ptr = buf;
        file->_bufsiz = size;
    } else {
        // no buffer provided, allocate a new
        io_buffer_allocate(file);
    }
    funlockfile(file);
    return 0;
}
