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

void io_buffer_allocate(FILE* stream)
{
    // Is the stream really buffered?
    if (stream->BufferMode != _IONBF) {
        stream->Base = calloc(1, BUFSIZ);
        if (stream->Base) {
            stream->BufferSize = BUFSIZ;
            stream->Flags |= _IOMYBUF;
        } else {
            stream->Flags &= ~(_IOMYBUF | _IOUSRBUF);
            stream->BufferMode = _IONBF;
        }
    }

    // ensure the buffer still set to charbuf
    if (stream->BufferMode == _IONBF) {
        stream->Base = (char*)(&stream->_charbuf);
        stream->BufferSize = sizeof(stream->_charbuf);
    }
    __FILE_ResetBuffer(stream);
}

void io_buffer_ensure(FILE* stream)
{
    if (stream->Base) {
        return;
    }
    io_buffer_allocate(stream);
}

oserr_t
io_buffer_flush(
    _In_ FILE* file)
{
    int bytesWritten;

    if (__FILE_IsStrange(file)) {
        return OS_ENOTSUPPORTED;
    }

    if (!__FILE_CanWrite(file)) {
        return OS_EPERMISSIONS;
    }

    // If there is no data to flush, then skip. This can happen for streams that
    // have no buffer, or just empty buffers.
    if (file->BytesValid == 0) {
        return OS_EOK;
    }

    // Do the actual flushing of the underlying IOD.
    bytesWritten = write(file->IOD, file->Base, (unsigned int)file->BytesValid);
    if (bytesWritten >= 0) {
        // Mark the stream as unmodified as long as there wasn't an error
        file->Flags &= ~(_IOMOD);
    } else {
        // Otherwise, there was an i/o error with the underlying implementation.
        file->Flags |= _IOERR;
        return OS_EDEVFAULT;
    }
    __FILE_ResetBuffer(file);
    return OS_EOK;
}

static void
__flush_entry(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct stdio_object_entry* entry  = element;
    stdio_handle_t*                  object = entry->handle;
    uint16_t                         flags  = *((uint16_t*)userContext);
    _CRT_UNUSED(index);
    if (object->Stream->Flags & flags) {
        fflush(object->Stream);
    }
}

void
io_buffer_flush_all(
        _In_ uint16_t flags)
{
    LOCK_FILES();
    hashtable_enumerate(
            stdio_get_handles(),
            __flush_entry,
            &flags
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
