/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *
 * Standard C Library
 * - Reads an array of count elements, 
 *   each one with a size of size bytes, from the stream and stores 
 *   them in the block of memory specified by ptr.
 */

#ifndef __TEST
//#define __TRACE
#include <ddk/utils.h>
#include <internal/_io.h>
#include <io.h>
#include <stdio.h>
#endif

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

static inline void __set_eof(stdio_handle_t* handle)
{
    if (handle->object.type == STDIO_HANDLE_FILE) {
        handle->wxflag |= WX_ATEOF;
    }
}

static int
__read_as_binary(stdio_handle_t* handle, char* buf, unsigned int count)
{
    char*      pointer = buf;
    size_t     bytesRead;
    oscode_t status;

    status = handle->ops.read(handle, pointer, count, &bytesRead);
    if (status == OsOK) {
        // Test against EOF
        if (count != 0 && bytesRead == 0) {
            __set_eof(handle);
        }
        return (int)bytesRead;
    }
    return -1;
}

/**
 * __get_utf8_character_bytes
 * Returns the number of bytes the utf8 character occupies
 * @param ch
 * @return
 */
static int
__get_utf8_character_bytes(char ch)
{
    if ((ch & 0xf8) == 0xf0)
        return 4;
    else if ((ch & 0xf0) == 0xe0)
        return 3;
    else if ((ch & 0xe0) == 0xc0)
        return 2;
    return 1;
}

/**
 * __read_as_utf8
 * @param handle
 * @param buf
 * @param count
 * @return
 */
static int
__read_as_utf8(stdio_handle_t* handle, wchar_t *buf, unsigned int count)
{
    char      min_buf[5], *readbuf, lookahead = '\0';
    size_t    readbuf_size, pos               = 0, bytesRead = 1, char_len, i, j;
	long long noppos;

    // make the buffer big enough to hold at least one character
    // read bytes have to fit to output and lookahead buffers
    count /= 2;
    readbuf_size = count < 4 ? 4 : count;
    if (readbuf_size <= 4 || !(readbuf = malloc(readbuf_size))) {
        readbuf_size = 4;
		readbuf = &min_buf[0];
		memset(readbuf, 0, sizeof(min_buf));
    }

    if (handle->lookahead[0] != '\n') {
        readbuf[pos++] = handle->lookahead[0];
        handle->lookahead[0] = '\n';

        if (handle->lookahead[1] != '\n') {
            readbuf[pos++] = handle->lookahead[1];
            handle->lookahead[1] = '\n';

            if (handle->lookahead[2] != '\n') {
                readbuf[pos++] = handle->lookahead[2];
                handle->lookahead[2] = '\n';
            }
        }
	}
	
	// Handle the small case with the local buffer
    if (count < 4) {
        if(!pos && handle->ops.read(handle, readbuf, 1, &bytesRead) == OsOK) {
            if(!bytesRead) {
                __set_eof(handle);
                if (readbuf != &min_buf[0]) {
                    free(readbuf);
                }
				return 0;
			}
            else {
				pos++;
			}
        }

		// Read the rest of the character bytes
        char_len = __get_utf8_character_bytes(readbuf[0]);
        if(char_len > pos) {
            if(handle->ops.read(handle, readbuf + pos, char_len - pos, &bytesRead) == OsOK) {
                pos += bytesRead;
			}
        }

		// Handle newline checks
        if(readbuf[0] == '\n') {
            handle->wxflag |= WX_READNL;
		}
        else {
            handle->wxflag &= ~WX_READNL;
		}

		// Check for ctrl-z
        if(readbuf[0] == 0x1a) {
            __set_eof(handle);
            if (readbuf != &min_buf[0]) {
                free(readbuf);
            }
            return 0;
        }

		// Handle CR
        if (readbuf[0] == '\r') {
			if (handle->ops.read(handle, &lookahead, 1, &bytesRead) == OsOK && bytesRead == 1) {
				buf[0] = '\r';
			}
			else if (lookahead == '\n') {
                buf[0] = '\n';
			}
			else {
                buf[0] = '\r';
                if(handle->wxflag & (WX_PIPE | WX_TTY)) {
                    handle->lookahead[0] = lookahead;
				}
                else {
                    handle->ops.seek(handle, SEEK_CUR, -1, &noppos);
				}
			}

            if (readbuf != &min_buf[0]) {
                free(readbuf);
            }
            return 2;
        }

		// Convert the mb to a wide-char
        bytesRead = mbstowcs(buf, readbuf, MIN(pos, count));
        if (bytesRead == 0 || bytesRead == (size_t)-1) {
            if (readbuf != &min_buf[0]) {
                free(readbuf);
            }
            return -1;
        }

        return (int)bytesRead * 2;
    }

    // perform the read operation to fill the read buffer up
    if (handle->ops.read(handle, readbuf + pos, readbuf_size - pos, &bytesRead) != OsOK) {
		// EOF?
		if (!pos && !bytesRead) {
            __set_eof(handle);
			if (readbuf != &min_buf[0]) {
				free(readbuf);
			}
			return 0;
		}

        if (pos) {
            bytesRead = 0;
		}
        else {
            if (readbuf != &min_buf[0]) {
				free(readbuf);
			}
            return -1;
        }
    }

	// Increase position and do a check for newline
    pos += bytesRead;
    if (readbuf[0] == '\n') {
        handle->wxflag |= WX_READNL;
	}
    else {
        handle->wxflag &= ~WX_READNL;
	}

    // Find first byte of last character (may be incomplete)
    for (i = pos - 1; i > 0 && i > (pos - 4); i--) {
        if ((readbuf[i] & 0xc0) != 0x80) {
			break;
		}
	}

	// Get the length of the read character
    char_len = __get_utf8_character_bytes(readbuf[i]);
    if (char_len + i <= pos) {
        i += char_len;
	}

	// If it's a terminal or pipe handle, use lookahead buffer
    if (handle->wxflag & (WX_PIPE | WX_TTY)) {
        if (i < pos) {
            handle->lookahead[0] = readbuf[i];
		}
        if (i+1 < pos) {
            handle->lookahead[1] = readbuf[i + 1];
		}
        if (i+2 < pos) {
            handle->lookahead[2] = readbuf[i + 2];
		}
    }
    else if (i < pos) {
        handle->ops.seek(handle, SEEK_CUR, i - pos, &noppos);
	}
	
	// Store i
    pos = i;

    for (i = 0, j = 0; i < pos; i++) {
		// Check for ctrl-z
        if (readbuf[i] == 0x1a) {
            __set_eof(handle);
            break;
        }

        // strip '\r' if followed by '\n'
        if (readbuf[i] == '\r' && (i + 1) == pos) {
			if (handle->lookahead[0] != '\n'
				|| handle->ops.read(handle, &lookahead, 1, &bytesRead) == OsOK) {
                readbuf[j++] = '\r';
			}
			else if (lookahead == '\n' && j == 0) {
                readbuf[j++] = '\n';
			}
			else {
                if (lookahead != '\n') {
                    readbuf[j++] = '\r';
				}

                if (handle->wxflag & (WX_PIPE | WX_TTY)) {
                    handle->lookahead[0] = lookahead;
				}
                else {
					handle->ops.seek(handle, SEEK_CUR, -1, &noppos);
				}
            }
		}
        else if (readbuf[i] != '\r' || readbuf[i + 1] != '\n') {
            readbuf[j++] = readbuf[i];
        }
	}
	
	// Store j as new position
    pos = j;

	// Convert to widechar string
    bytesRead = mbstowcs(buf, &readbuf[0], MIN(pos, count));
    if (bytesRead == 0 || bytesRead == (size_t)-1) {
        if (readbuf != &min_buf[0]) {
			free(readbuf);
		}
        return -1;
    }

	// Free buffer if neccessary
    if (readbuf != &min_buf[0]) {
		free(readbuf);
	}
    return (int)bytesRead * 2;
}

static int
__read_as_text_or_wide(stdio_handle_t* handle, char* buf, unsigned int count)
{
    char*     bufferPointer = (char*)buf;
    size_t    bytesRead     = 0;
    int       isUtf16;
    long long pos;

    // Determine if we are reading UTF16
    isUtf16 = (handle->wxflag & WX_UTF16) == WX_UTF16;
    if (isUtf16 && (count & 0x1)) {
        _set_errno(EINVAL);
        return -1;
    }

    // Now do the actual read of either binary or UTF16
    if (handle->lookahead[0] != '\n' ||
        handle->ops.read(handle, bufferPointer, count, &bytesRead) == OsOK) {
        if (handle->lookahead[0] != '\n') {
            bufferPointer[0]     = handle->lookahead[0];
            handle->lookahead[0] = '\n';

            if (isUtf16) {
                bufferPointer[1]     = handle->lookahead[1];
                handle->lookahead[1] = '\n';
            }

            if (count > (1 + isUtf16) && handle->ops.read(handle,
                                                          bufferPointer + 1 + isUtf16,
                                                          count - 1 - isUtf16,
                                                          &bytesRead) == OsOK) {
                bytesRead += 1 + isUtf16;
            }
            else {
                bytesRead = 1 + isUtf16;
            }
        }

        // If we get uneven bytes read ignore the last
        if (isUtf16 && (bytesRead & 1)) {
            bytesRead--;
        }

        // Test against EOF
        if (count != 0 && bytesRead == 0) {
            __set_eof(handle);
        }
        else {
            size_t i, j;

            // Detect reading newline
            if (bufferPointer[0] == '\n' && (!isUtf16 || bufferPointer[1] == 0)) {
                handle->wxflag |= WX_READNL;
            }
            else {
                handle->wxflag &= ~WX_READNL;
            }

            for (i = 0, j = 0; i < bytesRead; i += 1 + isUtf16)
            {
                // In text mode, a ctrl-z signals EOF
                if (bufferPointer[i] == 0x1a && (!isUtf16 || bufferPointer[i + 1] == 0)) {
                    __set_eof(handle);
                    break;
                }

                // In text mode, strip \r if followed by \n
                if (bufferPointer[i] == '\r'
                    && (!isUtf16 || bufferPointer[i + 1] == 0)
                    && (i + 1 + isUtf16 == bytesRead))
                {
                    char lookahead[2];
                    size_t localBytesRead;

                    lookahead[1] = '\n';
                    if (handle->ops.read(handle, lookahead, 1 + isUtf16, &localBytesRead) == OsOK
                        && localBytesRead) {
                        if (lookahead[0] == '\n' && (!isUtf16 || lookahead[1] == 0) && j == 0) {
                            bufferPointer[j++] = '\n';

                            if (isUtf16) {
                                bufferPointer[j++] = 0;
                            }
                        }
                        else {
                            if (lookahead[0] != '\n' || (isUtf16 && lookahead[1] != 0)) {
                                bufferPointer[j++] = '\r';

                                if (isUtf16) {
                                    bufferPointer[j++] = 0;
                                }
                            }

                            if (handle->wxflag & (WX_PIPE | WX_TTY)) {
                                if (lookahead[0] == '\n' && (!isUtf16 || !lookahead[1])) {
                                    bufferPointer[j++] = '\n';

                                    if (isUtf16) {
                                        bufferPointer[j++] = 0;
                                    }
                                }
                                else {
                                    handle->lookahead[0] = lookahead[0];
                                    handle->lookahead[1] = lookahead[1];
                                }
                            }
                            else {
                                handle->ops.seek(handle, SEEK_CUR, -1 - isUtf16, &pos);
                            }
                        }
                    }
                    else {
                        bufferPointer[j++] = '\r';

                        if (isUtf16) {
                            bufferPointer[j++] = 0;
                        }
                    }
                }
                else if ((bufferPointer[i] != '\r' || (isUtf16 && bufferPointer[i + 1] != 0)) ||
                         (bufferPointer[i + 1 + isUtf16] != '\n' || (isUtf16 && bufferPointer[i + 3] != 0))) {
                    bufferPointer[j++] = bufferPointer[i];
                    if (isUtf16) {
                        bufferPointer[j++] = bufferPointer[i + 1];
                    }
                }
            }

            // Update bytes read
            bytesRead = j;
        }
    }
    else {
        return -1;
    }
    return (int)bytesRead;
}


#ifndef __TEST

int
read(int fd, void* buffer, unsigned int len)
{
	stdio_handle_t* handle = stdio_handle_get(fd);
    TRACE("read(int fd=%i, buffer=0x%" PRIxIN ", len=%u)", fd, buffer, len);

    if (!handle) {
        _set_errno(EBADFD);
        return -1;
    }

    // Predetermine if we are eof or zero read
	if (len == 0 || (handle->wxflag & WX_ATEOF)) {
		return 0;
	}

    // handle binary mode
    if (!(handle->wxflag & WX_TEXT)) {
        return __read_as_binary(handle, buffer, len);
    }

	// Determine if we are reading UTF8
	if ((handle->wxflag & WX_UTF) == WX_UTF) {
		return __read_as_utf8(handle, (wchar_t*)buffer, len);
	}

	return __read_as_text_or_wide(handle, buffer, len);
}

size_t
fread(void *vptr, size_t size, size_t count, FILE *stream)
{
	size_t rcnt = size * count;
	size_t cread = 0;
	size_t pread = 0;
	TRACE("fread(vptr=0x%" PRIxIN ", size=%" PRIuIN ", count=%" PRIuIN ", stream=0x%" PRIxIN ", stream->_flag=%i)",
       vptr, size, count, stream, stream ? stream->_flag : 0);

	if (!rcnt) {
	    _set_errno(EINVAL);
		return 0;
	}

	_lock_stream(stream);
	if (stream_ensure_mode(_IOREAD, stream)) {
	    goto exit;
	}
    io_buffer_ensure(stream);

	// so check the buffer for any data
	if (IO_HAS_BUFFER_DATA(stream)) {
		int pcnt = (rcnt > stream->_cnt) ? stream->_cnt : rcnt;
		memcpy(vptr, stream->_ptr, pcnt);
		
		stream->_cnt -= pcnt;
		stream->_ptr += pcnt;
		
		cread += pcnt;
		rcnt -= pcnt;
		vptr = (char *)vptr + pcnt;
	}

	// Keep reading untill all requested bytes are read, or EOF
	while (rcnt > 0) {
		int i;

		// if buffer is empty and the data fits into the buffer, then we fill that instead
		if (IO_IS_BUFFERED(stream) && !stream->_cnt && rcnt < BUFSIZ) {
			stream->_cnt = read(stream->_fd, stream->_base, stream->_bufsiz);
			stream->_ptr = stream->_base;
			i = (stream->_cnt < rcnt) ? stream->_cnt : rcnt;

			/* If the buffer fill reaches eof but fread wouldn't, clear eof. */
			if (i > 0 && i < stream->_cnt) {
				stdio_handle_get(stream->_fd)->wxflag &= ~WX_ATEOF;
				stream->_flag &= ~_IOEOF;
			}
			
			if (i > 0) {
				memcpy(vptr, stream->_ptr, i);
				stream->_cnt -= i;
				stream->_ptr += i;
			}
		}
		else if (rcnt > INT_MAX) {
			i = read(stream->_fd, vptr, INT_MAX);
		}
		else if (rcnt < BUFSIZ) {
			i = read(stream->_fd, vptr, rcnt);
		}
		else {
			i = read(stream->_fd, vptr, rcnt - BUFSIZ / 2);
		}

		// update iterators
		pread += i;
		rcnt -= i;
		vptr = (char *)vptr + i;

		// Check for EOF condition
		// also for error conditions
		if (stdio_handle_get(stream->_fd)->wxflag & WX_ATEOF) {
			stream->_flag |= _IOEOF;
		}
		else if (i == -1) {
			stream->_flag |= _IOERR;
			pread = 0;
			rcnt = 0;
		}

		// Break if bytes read is 0 or below
		if (i < 1) {
			break;
		}
	}

	// Increase the number of bytes read
	cread += pread;

exit:
    _unlock_stream(stream);
    TRACE("fread returns=%" PRIuIN ", errno=%i", (cread / size), errno);
	return (cread / size);
}

#endif
