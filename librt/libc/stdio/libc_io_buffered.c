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
#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <internal/_io.h>
#include <internal/_file.h>
#include <io.h>
#include <stdlib.h>

extern hashtable_t* stdio_get_handles(void);

void io_buffer_allocate(FILE* stream)
{
    if (!(stream->_flag & _IONBF)) {
        stream->_base = calloc(1, BUFSIZ);
        if (stream->_base) {
            stream->_bufsiz = BUFSIZ;
            stream->_flag |= _IOMYBUF;
        }
        else {
            stream->_flag &= ~(_IONBF | _IOMYBUF | _USERBUF | _IOLBF);
            stream->_flag |= _IONBF;
        }
    }

    // ensure the buffer still set to charbuf
    if (stream->_flag & _IONBF) {
        stream->_base = (char *)(&stream->_charbuf);
        stream->_bufsiz = 2;
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
    if ((file->_flag & (_IOREAD | _IOWRT)) == _IOWRT && file->_flag & (_IOMYBUF | _USERBUF)) {
        size_t cnt = (size_t)(file->_ptr - file->_base);

        // Flush them
        if (cnt > 0 && write(file->_fd, file->_base, (unsigned int)cnt) != cnt) {
            file->_flag |= _IOERR;
            return OS_EUNKNOWN;
        }

        // If it's rw, clear WRITE flag
        if (file->_flag & _IORW) {
            file->_flag &= ~_IOWRT;
        }
        file->_ptr = file->_base;
        file->_cnt = 0;
    }
    return OS_EOK;
}

struct __flush_context {
    int mask;
    int files_flushed;
};

static void __flush_entry(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct stdio_object_entry* entry   = element;
    stdio_handle_t*                  object  = entry->handle;
    struct __flush_context*          context = userContext;
    FILE*                            file    = object->buffered_stream;
    _CRT_UNUSED(index);

    if (file && (file->_flag & context->mask)) {
        fflush(file);
        context->files_flushed++;
    }
}

int
io_buffer_flush_all(
    _In_ int mask)
{
    struct __flush_context context = {
            .mask = mask,
            .files_flushed = 0
    };

    LOCK_FILES();
    hashtable_enumerate(
            stdio_get_handles(),
            __flush_entry,
            &context
    );
    UNLOCK_FILES();
    return context.files_flushed;
}

void flockfile(FILE* stream)
{
    assert(stream != NULL);
    usched_mtx_lock(&stream->_lock);
}

int ftrylockfile(FILE* stream)
{
    if (stream == NULL) {
        return OS_EINVALPARAMS;
    }
    return usched_mtx_trylock(&stream->_lock);
}

void funlockfile(FILE* stream)
{
    assert(stream != NULL);
    usched_mtx_unlock(&stream->_lock);
}
