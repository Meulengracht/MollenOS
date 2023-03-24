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

//#define __TRACE

#include <assert.h>
#include <ds/hashtable.h>
#include <internal/_io.h>
#include <internal/_file.h>
#include <io.h>
#include <stdlib.h>

extern hashtable_t* stdio_get_handles(void);

void  io_buffer_allocate(FILE* stream)
{
    // Is the stream really buffered?
    if (stream->BufferMode != _IONBF) {
        stream->_base = calloc(1, BUFSIZ);
        if (stream->_base) {
            stream->_bufsiz = BUFSIZ;
            stream->Flags |= _IOMYBUF;
        } else {
            stream->Flags &= ~(_IOMYBUF | _IOUSRBUF);
            stream->BufferMode = _IONBF;
        }
    }

    // ensure the buffer still set to charbuf
    if (stream->BufferMode == _IONBF) {
        stream->_base = (char*)(&stream->_charbuf);
        stream->_bufsiz = sizeof(stream->_charbuf);
    }

    stream->_ptr = stream->_base;
    stream->_cnt = 0;
}

void io_buffer_ensure(FILE* stream)
{
    if (stream->_base) {
        return;
    }
    io_buffer_allocate(stream);
}

oserr_t
io_buffer_flush(
    _In_ FILE* file)
{
    size_t bytesToFlush;
    int    bytesWritten;

    if (__FILE_IsStrange(file)) {
        return OS_ENOTSUPPORTED;
    }

    if (file->StreamMode != __STREAMMODE_WRITE) {
        return OS_EOK;
    }

    bytesToFlush = (size_t)(file->_ptr - file->_base);
    if (bytesToFlush == 0) {
        return OS_EOK;
    }

    bytesWritten = write(file->IOD, file->_base, (unsigned int)bytesToFlush);
    if (bytesWritten != bytesToFlush) {
        file->Flags |= _IOERR;
        return OS_EDEVFAULT;
    }

    file->_ptr = file->_base;
    file->_cnt = file->_bufsiz;
    return OS_EOK;
}

static void
__flush_entry(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct stdio_object_entry* entry   = element;
    stdio_handle_t*                  object  = entry->handle;
    uint8_t                          streamMode = *((uint8_t*)userContext);
    _CRT_UNUSED(index);
    if (object->Stream->StreamMode & streamMode) {
        fflush(object->Stream);
    }
}

void
io_buffer_flush_all(
        _In_ uint8_t streamMode)
{
    LOCK_FILES();
    hashtable_enumerate(
            stdio_get_handles(),
            __flush_entry,
            &streamMode
    );
    UNLOCK_FILES();
}

void flockfile(FILE* stream)
{
    assert(stream != NULL);
    usched_mtx_lock(&stream->Lock);
}

int ftrylockfile(FILE* stream)
{
    if (stream == NULL) {
        return OS_EINVALPARAMS;
    }
    return usched_mtx_trylock(&stream->Lock);
}

void funlockfile(FILE* stream)
{
    assert(stream != NULL);
    usched_mtx_unlock(&stream->Lock);
}
