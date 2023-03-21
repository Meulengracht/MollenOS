/*
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS crt library
 * FILE:            lib/sdk/crt/stdio/_flsbuf.c
 * PURPOSE:         Implementation of _flsbuf / _flswbuf
 * PROGRAMMER:      Timo Kreuzer
 */

//#define __TRACE

#include "ddk/utils.h"
#include "io.h"
#include "internal/_file.h"
#include "internal/_io.h"
#include "stdio.h"

static inline int
__can_flush_otherwise_set_IOERR(FILE *stream)
{
    if ((stream->_flag & _IOSTRG) || !(stream->_flag & (_IORW|_IOWRT))) {
        stream->_flag |= _IOERR;
        return EOF;
    }
    return 0;
}

static int
__prepare_flush_otherwise_set_IOERR(FILE *stream)
{
    stream->_cnt = 0;

    // Check if this was also a read buffer
    if (stream->_flag & _IOREAD) { // Must be at the end of the file
        if (!(stream->_flag & _IOEOF)) {
            stream->_flag |= _IOERR;
            return EOF;
        }
        stream->_ptr = stream->_base;
    }

    stream->_flag &= ~(_IOREAD|_IOEOF);
    stream->_flag |= _IOWRT;

    io_buffer_ensure(stream);
    return 0;
}

int
_flsbuf(
    _In_ int   ch,
    _In_ FILE* stream)
{
    int   count, written, res;
    TCHAR charTyped = (TCHAR)(ch & (sizeof(TCHAR) > sizeof(char) ? 0xffff : 0xff));
    TRACE("_flsbuf(ch=%i, stream=0x%" PRIxIN ", stream->_flag=0x%x)", ch, stream, stream->_flag);

    // Check if the stream supports flushing
    res = __can_flush_otherwise_set_IOERR(stream);
    if (res) {
        TRACE("_flsbuf return=%i", res);
        return res;
    }

    // lock file and reset count
    res = __prepare_flush_otherwise_set_IOERR(stream);
    if (res) {
        TRACE("_flsbuf return=%i", res);
        return res;
    }

    // did we get a buffer and is stream buffered?
    if (stream->_base && !(stream->_flag & _IONBF)) {
        count = (int)(stream->_ptr - stream->_base);
        if (count > 0)
            written = write(stream->_fd, stream->_base, count);
        else
            written = 0;

        /* Reset buffer and put the char into it */
        stream->_ptr = stream->_base + sizeof(TCHAR);
        stream->_cnt = stream->_bufsiz - sizeof(TCHAR);
        *(TCHAR*)stream->_base = charTyped;
    } else {
        // no buffer, write directly
        count   = sizeof(TCHAR);
        written = write(stream->_fd, &ch, sizeof(TCHAR));
    }

    // Did we not flush what was expected?
    if (written != count) {
        stream->_flag |= _IOERR;
        charTyped = EOF;
    }

    TRACE("_flsbuf return=%i", (int)charTyped);
    return charTyped;
}
