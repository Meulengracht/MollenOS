/*
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS crt library
 * FILE:            lib/sdk/crt/stdio/_flsbuf.c
 * PURPOSE:         Implementation of _flsbuf / _flswbuf
 * PROGRAMMER:      Timo Kreuzer
 */

#include <stdio.h>
#include <io.h>
#include "local.h"

int
_flsbuf(
    _In_ int ch, 
    _In_ FILE *stream)
{
    int count, written;

    // Check if the stream supports flushing
    if ((stream->_flag & _IOSTRG) || !(stream->_flag & (_IORW|_IOWRT))) {
        stream->_flag |= _IOERR;
        return EOF;
    }

    // lock file and reset count
    _lock_file(stream);
    stream->_cnt = 0;

    // Check if this was a read buffer
    if (stream->_flag & _IOREAD) { // Must be at the end of the file
        if (!(stream->_flag & _IOEOF)) {
            stream->_flag |= _IOERR;
            _unlock_file(stream);
            return EOF;
        }
        stream->_ptr = stream->_base;
    }
    stream->_flag &= ~(_IOREAD|_IOEOF);
    stream->_flag |= _IOWRT;

    // Check if should get a buffer
    if (!(stream->_flag & _IONBF) && stream != stdout && stream != stderr) {
        if (!stream->_base) {
            os_alloc_buffer(stream);
        }
    }

    /* Check if we can use a buffer now */
    if (stream->_base && !(stream->_flag & _IONBF)) {
        /* We can, check if there is something to write */
        count = (int)(stream->_ptr - stream->_base);
        if (count > 0)
            written = write(stream->_fd, stream->_base, count);
        else
            written = 0;

        /* Reset buffer and put the char into it */
        stream->_ptr = stream->_base + sizeof(TCHAR);
        stream->_cnt = stream->_bufsiz - sizeof(TCHAR);
        *(TCHAR*)stream->_base = ch;
    }
    else {
        /* There is no buffer, write the char directly */
        count = sizeof(TCHAR);
        written = write(stream->_fd, &ch, sizeof(TCHAR));
    }

    /* Check for failure */
    if (written != count) {
        stream->_flag |= _IOERR;
        _unlock_file(stream);
        return EOF;
    }
    _unlock_file(stream);
    return ch & (sizeof(TCHAR) > sizeof(char) ? 0xffff : 0xff);
}
