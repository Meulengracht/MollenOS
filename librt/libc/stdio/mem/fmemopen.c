/**
 * Copyright 2023, Philip Meulengracht
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
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <internal/_io.h>
#include <io.h>
#include "../private.h"

struct MemoryStream {
    void*  Buffer;
    size_t Capacity;
    bool   Cleanup;
};



static stdio_ops_t g_memoryOps = {
    .inherit
};

static struct MemoryStream*
__memstream_new(
        _In_ void*  buffer,
        _In_ size_t length)
{

}

static void
__memstream_delete(
        _In_ struct MemoryStream* memoryStream)
{
    if (memoryStream == NULL) {
        return;
    }

    if (memoryStream->Cleanup) {
        free(memoryStream->Buffer);
    }
    free(memoryStream);
}

FILE* fmemopen(void *buf, size_t size, const char *mode)
{
    int                  flags;
    struct MemoryStream* memoryStream;
    stdio_handle_t*      object;
    int                  status;

    if (size == 0 || mode == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (__fmode_to_flags(mode, &flags)) {
        errno = EINVAL;
        return NULL;
    }

    memoryStream = __memstream_new(buf, size);
    if (memoryStream == NULL) {
        return NULL;
    }

    status = stdio_handle_create2(
            -1,
            flags,
            0,
            MEMORYSTREAM_SIGNATURE,
            &g_memoryOps,
            memoryStream,
            &object
    );
    if (status) {
        __memstream_delete(memoryStream);
        return NULL;
    }

    status = stdio_handle_set_buffered(object, NULL, _IOFBF);
    if (status) {
        stdio_handle_delete(object);
        __memstream_delete(memoryStream);
        return NULL;
    }
    return stdio_handle_stream(object);
}
