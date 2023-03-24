/*
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS crt library
 * FILE:            lib/sdk/crt/stdio/_flsbuf.c
 * PURPOSE:         Implementation of _flsbuf / _flswbuf
 * PROGRAMMER:      Timo Kreuzer
 */

//#define __TRACE

#include <ddk/utils.h>
#include <errno.h>
#include <io.h>
#include <internal/_file.h>
#include <internal/_io.h>
#include <stdio.h>

// Writes a character into a buffered stream.
int
_flsbuf(
    _In_ int   ch,
    _In_ FILE* stream)
{
    int   count, written;
    TCHAR charTyped = (TCHAR)(ch & (sizeof(TCHAR) > sizeof(char) ? 0xffff : 0xff));
    TRACE("_flsbuf(ch=%i, stream=0x%" PRIxIN ", stream->_flag=0x%x)", ch, stream, stream->Flags);

    // We cannot flush strange file streams
    if (__FILE_IsStrange(stream)) {
        errno = EACCES;
        return -1;
    }

    // did we get a buffer and is stream buffered?
    if (stream->_base && !(stream->Flags & _IONBF)) {
        count = (int)(stream->_ptr - stream->_base);
        if (count > 0)
            written = write(stream->IOD, stream->_base, count);
        else
            written = 0;

        /* Reset buffer and put the char into it */
        stream->_ptr = stream->_base + sizeof(TCHAR);
        stream->_cnt = stream->_bufsiz - sizeof(TCHAR);
        *(TCHAR*)stream->_base = charTyped;
    } else {
        // no buffer, write directly
        count   = sizeof(TCHAR);
        written = write(stream->IOD, &ch, sizeof(TCHAR));
    }

    // Did we not flush what was expected?
    if (written != count) {
        stream->Flags |= _IOERR;
        charTyped = EOF;
    }

    TRACE("_flsbuf return=%i", (int)charTyped);
    return charTyped;
}
