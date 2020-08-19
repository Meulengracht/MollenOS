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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Standard C Library
 * - Reads an array of count elements, 
 *   each one with a size of size bytes, from the stream and stores 
 *   them in the block of memory specified by ptr.
 */

#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <internal/_io.h>

/* GetUtf8CharacterLength
 * Derives how many bytes the UTF8 character is */
static int GetUtf8CharacterLength(char ch)
{
    if((ch&0xf8) == 0xf0)
        return 4;
    else if((ch&0xf0) == 0xe0)
        return 3;
    else if((ch&0xe0) == 0xc0)
        return 2;
    return 1;
}

/* _ReadUtf8
 * Reads an UTF8 character from the given file descriptor */
static int _ReadUtf8(int fd, wchar_t *buf, unsigned int count)
{
    char min_buf[5], *readbuf, lookahead = '\0';
    size_t readbuf_size, pos = 0, BytesRead = 1, char_len, i, j;
	stdio_handle_t *handle = stdio_handle_get(fd);
	long long noppos;
	
    if (handle == NULL) {
        _set_errno(EBADFD);
        return -1;
    }

    // make the buffer big enough to hold at least one character
    // read bytes have to fit to output and lookahead buffers
    count /= 2;
    readbuf_size = count < 4 ? 4 : count;
    if(readbuf_size<=4 || !(readbuf = malloc(readbuf_size))) {
        readbuf_size = 4;
		readbuf = &min_buf[0];
		memset(readbuf, 0, sizeof(min_buf));
    }

    if(handle->lookahead[0] != '\n') {
        readbuf[pos++] = handle->lookahead[0];
        handle->lookahead[0] = '\n';

        if(handle->lookahead[1] != '\n') {
            readbuf[pos++] = handle->lookahead[1];
            handle->lookahead[1] = '\n';

            if(handle->lookahead[2] != '\n') {
                readbuf[pos++] = handle->lookahead[2];
                handle->lookahead[2] = '\n';
            }
        }
	}
	
	// Handle the small case with the local buffer
    if(count < 4) {
        if(!pos && handle->ops.read(handle, readbuf, 1, &BytesRead) == OsSuccess) {
            if(!BytesRead) {
				handle->wxflag |= WX_ATEOF;
                if (readbuf != &min_buf[0]) {
                    free(readbuf);
                }
				return 0;
			}else {
				pos++;
			}
        }

		// Read the rest of the character bytes
        char_len = GetUtf8CharacterLength(readbuf[0]);
        if(char_len > pos) {
            if(handle->ops.read(handle, readbuf + pos, char_len - pos, &BytesRead) == OsSuccess) {
                pos += BytesRead;
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
            handle->wxflag |= WX_ATEOF;
            if (readbuf != &min_buf[0]) {
                free(readbuf);
            }
            return 0;
        }

		// Handle CR
        if(readbuf[0] == '\r') {
			if(handle->ops.read(handle, &lookahead, 1, &BytesRead) == OsSuccess 
				&& BytesRead == 1) {
				buf[0] = '\r';
			}
            else if(lookahead == '\n') {
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
        if(!(BytesRead = mbstowcs(buf, readbuf, MIN(pos, count)))) {
            if (readbuf != &min_buf[0]) {
                free(readbuf);
            }
            return -1;
        }

        return BytesRead * 2;
    }

    if(handle->ops.read(handle, readbuf+pos, readbuf_size-pos, &BytesRead) == OsSuccess) {
		// EOF?
		if(!pos && !BytesRead) {
			handle->wxflag |= WX_ATEOF;

			if (readbuf != &min_buf[0]) {
				free(readbuf);
			}
			return 0;
		}

        if(pos) {
            BytesRead = 0;
		}
		else {
            if (readbuf != &min_buf[0]) {
				free(readbuf);
			}
            return -1;
        }
    }

	// Increase position and do a check for newline
    pos += BytesRead;
    if(readbuf[0] == '\n') {
        handle->wxflag |= WX_READNL;
	}
    else {
        handle->wxflag &= ~WX_READNL;
	}

    // Find first byte of last character (may be incomplete)
    for(i = pos - 1; i > 0 && i > (pos - 4); i--) {
        if((readbuf[i] & 0xc0) != 0x80) {
			break;
		}
	}

	// Get the length of the read character
    char_len = GetUtf8CharacterLength(readbuf[i]);
    if(char_len + i <= pos) {
        i += char_len;
	}

	// If it's a terminal or pipe handle, use lookahead buffer
    if(handle->wxflag & (WX_PIPE | WX_TTY)) {
        if(i < pos) {
            handle->lookahead[0] = readbuf[i];
		}
        if(i+1 < pos) {
            handle->lookahead[1] = readbuf[i + 1];
		}
        if(i+2 < pos) {
            handle->lookahead[2] = readbuf[i + 2];
		}
    }else if(i < pos) {
        handle->ops.seek(handle, SEEK_CUR, i - pos, &noppos);
	}
	
	// Store i
    pos = i;

    for(i = 0, j = 0; i < pos; i++) {
		// Check for ctrl-z
        if(readbuf[i] == 0x1a) {
            handle->wxflag |= WX_ATEOF;
            break;
        }

        // strip '\r' if followed by '\n'
        if(readbuf[i] == '\r' && (i + 1) == pos) {
			if(handle->lookahead[0] != '\n' 
				|| handle->ops.read(handle, &lookahead, 1, &BytesRead) == OsSuccess) {
                readbuf[j++] = '\r';
			}
			else if(lookahead == '\n' && j == 0) {
                readbuf[j++] = '\n';
			}
			else {
                if(lookahead != '\n') {
                    readbuf[j++] = '\r';
				}

                if(handle->wxflag & (WX_PIPE | WX_TTY)) {
                    handle->lookahead[0] = lookahead;
				}
                else {
					handle->ops.seek(handle, SEEK_CUR, -1, &noppos);
				}
            }
		}
		else if(readbuf[i]!='\r' || readbuf[i+1]!='\n') {
            readbuf[j++] = readbuf[i];
        }
	}
	
	// Store j as new position
    pos = j;

	// Convert to widechar string
    if(!(BytesRead = mbstowcs(buf, &readbuf[0], MIN(pos, count)))) {
        if (readbuf != &min_buf[0]) {
			free(readbuf);
		}
        return -1;
    }

	// Free buffer if neccessary
    if (readbuf != &min_buf[0]) {
		free(readbuf);
	}
    return BytesRead * 2;
}

/* read
 * returns the number of bytes read, which might be less than 
 * count if there are fewer than count bytes left in the file or if the file 
 * was opened in text mode, in which case each carriage return–line feed 
 * (CR-LF) pair is replaced with a single linefeed character. 
 * Only the single linefeed character is counted in the return value. 
 * The replacement does not affect the file pointer. */
int read(int fd, void* buffer, unsigned int len)
{
	stdio_handle_t* handle       = stdio_handle_get(fd);
	char*           BufferCursor = (char*)buffer;
	size_t          BytesRead;
	size_t          Utf16;
	long long       pos;

    if (!handle) {
        _set_errno(EBADFD);
        return -1;
    }

    // Predetermine if we are eof or zero read
	if (len == 0 || (handle->wxflag & WX_ATEOF)) {
		return 0;
	}

	// Determine if we are reading UTF16
	Utf16 = (handle->wxflag & (WX_WIDE | WX_UTF)) == (WX_WIDE | WX_UTF);
	if (((handle->wxflag & WX_UTF) || Utf16) && (len & 0x1)) {
		_set_errno(EINVAL);
		return 4;
	}

	// Determine if we are reading UTF8
	if ((handle->wxflag & WX_UTF) && !(handle->wxflag & WX_WIDE)) {
		return _ReadUtf8(fd, (wchar_t*)buffer, len);
	}

	// Now do the actual read of either binary or UTF16
	if (handle->lookahead[0] != '\n' ||
        handle->ops.read(handle, BufferCursor, len, &BytesRead) == OsSuccess) {
		if (handle->lookahead[0] != '\n') {
			BufferCursor[0] = handle->lookahead[0];
			handle->lookahead[0] = '\n';

			if (Utf16) {
				BufferCursor[1] = handle->lookahead[1];
				handle->lookahead[1] = '\n';
			}

			if (len > (1 + Utf16) && handle->ops.read(handle,
				BufferCursor + 1 + Utf16, len - 1 - Utf16, &BytesRead) == OsSuccess) {
				BytesRead += 1 + Utf16;			
			}
			else {
				BytesRead = 1 + Utf16;
			}
		}

		// If we get uneven bytes read ignore the last
		if (Utf16 && (BytesRead & 1)) {
			BytesRead--;
		}

		// Test against EOF
		if (len != 0 && BytesRead == 0) {
			handle->wxflag |= WX_ATEOF;
		}
		else if (handle->wxflag & WX_TEXT) {
			// Variables
			size_t i, j;

			// Detect reading newline
			if (BufferCursor[0] == '\n' && (!Utf16 || BufferCursor[1] == 0)) {
				handle->wxflag |= WX_READNL;
			}
			else {
				handle->wxflag &= ~WX_READNL;
			}

			for (i = 0, j = 0; i < BytesRead; i += 1 + Utf16)
			{
				// In text mode, a ctrl-z signals EOF
				if (BufferCursor[i] == 0x1a && (!Utf16 || BufferCursor[i + 1] == 0)) {
					handle->wxflag |= WX_ATEOF;
					break;
				}

				// In text mode, strip \r if followed by \n
				if (BufferCursor[i] == '\r'
					&& (!Utf16 || BufferCursor[i + 1] == 0) 
					&& (i + 1 + Utf16 == BytesRead))
				{
					char lookahead[2];
					size_t count;

					lookahead[1] = '\n';
					if (handle->ops.read(handle, lookahead, 1 + Utf16, &count) == OsSuccess 
						&& count) {
						if (lookahead[0] == '\n' 
							&& (!Utf16 || lookahead[1] == 0) 
							&& j == 0) {
							BufferCursor[j++] = '\n';

							if (Utf16) {
								BufferCursor[j++] = 0;
							}
						}
						else {
							if (lookahead[0] != '\n' || (Utf16 && lookahead[1] != 0)) {
								BufferCursor[j++] = '\r';

								if (Utf16) {
									BufferCursor[j++] = 0;
								}
							}

							if (handle->wxflag & (WX_PIPE | WX_TTY)) {
								if (lookahead[0] == '\n' && (!Utf16 || !lookahead[1])) {
									BufferCursor[j++] = '\n';

									if (Utf16) {
										BufferCursor[j++] = 0;
									}
								}
								else {
									handle->lookahead[0] = lookahead[0];
									handle->lookahead[1] = lookahead[1];
								}
							}
							else {
								handle->ops.seek(handle, SEEK_CUR, -1 - Utf16, &pos);
							}
						}
					}
					else {
						BufferCursor[j++] = '\r';
						
						if (Utf16) {
							BufferCursor[j++] = 0;
						}
					}
				}
				else if ((BufferCursor[i] != '\r' 
					|| (Utf16 && BufferCursor[i + 1] != 0)) 
					|| (BufferCursor[i + 1 + Utf16] != '\n' 
					|| (Utf16 && BufferCursor[i + 3] != 0))) {
					BufferCursor[j++] = BufferCursor[i];
					
					if (Utf16) {
						BufferCursor[j++] = BufferCursor[i + 1];
					}
				}
			}

			// Update bytes read
			BytesRead = j;
		}
	}
	else {
		return -1;
	}
	return (int)BytesRead;
}

/* fread
 * Reads an array of count elements, 
 * each one with a size of size bytes, from the stream and stores 
 * them in the block of memory specified by ptr. */
size_t fread(void *vptr, size_t size, size_t count, FILE *stream)
{
	size_t rcnt = size * count;
	size_t cread = 0;
	size_t pread = 0;

	if (!rcnt) {
		return 0;
	}

	// Lock file access
	_lock_file(stream);

	// Check if we have any buffered data already
	if (stream->_cnt > 0) {
		int pcnt = (rcnt > stream->_cnt) ? stream->_cnt : rcnt;
		memcpy(vptr, stream->_ptr, pcnt);
		
		stream->_cnt -= pcnt;
		stream->_ptr += pcnt;
		
		cread += pcnt;
		rcnt -= pcnt;
		vptr = (char *)vptr + pcnt;
	}
	// Detect if stream was opened in correct mode
	else if (!(stream->_flag & _IOREAD)) {
		if (stream->_flag & _IORW) {
			stream->_flag |= _IOREAD;
		}
		else {
			_unlock_file(stream);
			return 0;
		}
	}

	// Do we need to allocate a buffer?
	if (rcnt > 0 && !(stream->_flag & (_IONBF | _IOMYBUF | _USERBUF))) {
		os_alloc_buffer(stream);
	}

	// Keep reading untill all requested bytes are read, or EOF
	while (rcnt > 0) {
		int i;
		if (!stream->_cnt && rcnt < BUFSIZ && (stream->_flag & (_IOMYBUF | _USERBUF))) {
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

		// Update iterators
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

	// Unlock file and return amount read
	_unlock_file(stream);
	return (cread / size);
}
