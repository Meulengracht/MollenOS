/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - C Standard Library
 * - Reads an array of count elements, 
 *   each one with a size of size bytes, from the stream and stores 
 *   them in the block of memory specified by ptr.
 */

/* Includes 
 * - Library */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "local.h"

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

static int _ReadUtf8(int fd, wchar_t *buf, unsigned int count)
{
	// Variables
    char min_buf[5], *readbuf, lookahead;
    size_t readbuf_size, pos=0, BytesRead = 1, char_len, i, j;
	ioobject *fdinfo = get_ioinfo(fd);
	
    // make the buffer big enough to hold at least one character
    // read bytes have to fit to output and lookahead buffers
    count /= 2;
    readbuf_size = count < 4 ? 4 : count;
    if(readbuf_size<=4 || !(readbuf = malloc(readbuf_size))) {
        readbuf_size = 4;
		readbuf = &min_buf[0];
		memset(readbuf, 0, sizeof(min_buf));
    }

    if(fdinfo->lookahead[0] != '\n') {
        readbuf[pos++] = fdinfo->lookahead[0];
        fdinfo->lookahead[0] = '\n';

        if(fdinfo->lookahead[1] != '\n') {
            readbuf[pos++] = fdinfo->lookahead[1];
            fdinfo->lookahead[1] = '\n';

            if(fdinfo->lookahead[2] != '\n') {
                readbuf[pos++] = fdinfo->lookahead[2];
                fdinfo->lookahead[2] = '\n';
            }
        }
	}
	
	// Handle the small case with the local buffer
    if(count < 4) {
        if(!pos && StdioReadInternal(fd, readbuf, 1, &BytesRead) == OsSuccess) {
            if(!BytesRead) {
				fdinfo->wxflag |= WX_ATEOF;
				return 0;
			}else {
				pos++;
			}
        }

		// Read the rest of the character bytes
        char_len = GetUtf8CharacterLength(readbuf[0]);
        if(char_len > pos) {
            if(StdioReadInternal(fd, readbuf + pos, char_len - pos, &BytesRead) == OsSuccess) {
                pos += BytesRead;
			}
        }

		// Handle newline checks
        if(readbuf[0] == '\n') {
            fdinfo->wxflag |= WX_READNL;
		}
        else {
            fdinfo->wxflag &= ~WX_READNL;
		}

		// Check for ctrl-z
        if(readbuf[0] == 0x1a) {
            fdinfo->wxflag |= WX_ATEOF;
            return 0;
        }

		// Handle CR
        if(readbuf[0] == '\r') {
			if(StdioReadInternal(fd, &lookahead, 1, &BytesRead) == OsSuccess 
				&& BytesRead == 1) {
				buf[0] = '\r';
			}
            else if(lookahead == '\n') {
                buf[0] = '\n';
			}
            else {
                buf[0] = '\r';
                if(fdinfo->wxflag & (WX_PIPE | WX_TTY)) {
                    fdinfo->lookahead[0] = lookahead;
				}
                else {
                    StdioSeekInternal(fd, -1, SEEK_CUR);
				}
			}
			
            return 2;
        }

		// Convert the mb to a wide-char
        if(!(BytesRead = mbstowcs(buf, readbuf, count))) {
            return -1;
        }

        return BytesRead * 2;
    }

    if(StdioReadInternal(fd, readbuf+pos, readbuf_size-pos, &BytesRead) == OsSuccess) {
		// EOF?
		if(!pos && !BytesRead) {
			fdinfo->wxflag |= WX_ATEOF;

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

    pos += BytesRead;
    if(readbuf[0] == '\n')
        fdinfo->wxflag |= WX_READNL;
    else
        fdinfo->wxflag &= ~WX_READNL;

    /* Find first byte of last character (may be incomplete) */
    for(i=pos-1; i>0 && i>pos-4; i--)
        if((readbuf[i]&0xc0) != 0x80)
            break;
    char_len = GetUtf8CharacterLength(readbuf[i]);
    if(char_len+i <= pos)
        i += char_len;

    if(fdinfo->wxflag & (WX_PIPE | WX_TTY)) {
        if(i < pos)
            fdinfo->lookahead[0] = readbuf[i];
        if(i+1 < pos)
            fdinfo->lookahead[1] = readbuf[i+1];
        if(i+2 < pos)
            fdinfo->lookahead[2] = readbuf[i+2];
    }else if(i < pos) {
        SetFilePointer(fdinfo->handle, i-pos, NULL, FILE_CURRENT);
    }
    pos = i;

    for(i=0, j=0; i<pos; i++) {
        if(readbuf[i] == 0x1a) {
            fdinfo->wxflag |= WX_ATEOF;
            break;
        }

        /* strip '\r' if followed by '\n' */
        if(readbuf[i] == '\r' && i+1==pos) {
            if(fdinfo->lookahead[0] != '\n' || !ReadFile(hand, &lookahead, 1, &num_read, NULL) || !num_read) {
                readbuf[j++] = '\r';
            }else if(lookahead == '\n' && j==0) {
                readbuf[j++] = '\n';
            }else {
                if(lookahead != '\n')
                    readbuf[j++] = '\r';

                if(fdinfo->wxflag & (WX_PIPE | WX_TTY))
                    fdinfo->lookahead[0] = lookahead;
                else
                    SetFilePointer(fdinfo->handle, -1, NULL, FILE_CURRENT);
            }
        }else if(readbuf[i]!='\r' || readbuf[i+1]!='\n') {
            readbuf[j++] = readbuf[i];
        }
    }
    pos = j;

    if(!(num_read = MultiByteToWideChar(CP_UTF8, 0, readbuf, pos, buf, count))) {
        if (readbuf != min_buf) free(readbuf);
        return -1;
    }

    if (readbuf != min_buf) free(readbuf);
    return num_read*2;
}

/* _read
 * returns the number of bytes read, which might be less than 
 * count if there are fewer than count bytes left in the file or if the file 
 * was opened in text mode, in which case each carriage return–line feed 
 * (CR-LF) pair is replaced with a single linefeed character. 
 * Only the single linefeed character is counted in the return value. 
 * The replacement does not affect the file pointer. */
int _read(
	_In_ int fd, 
	_In_ void *buffer, 
	_In_ unsigned int len)
{
	// Variables
	size_t BytesRead, Utf16;
	char *BufferCursor = (char *)buffer;
	ioobject *fdinfo = get_ioinfo(fd);

	// Sanitize parameters
	if (len == 0 || (fdinfo->wxflag & WX_ATEOF)) {
		return 0;
	}

	// Determine if we are reading UTF16
	Utf16 = (fdinfo->exflag & EF_UTF16) != 0;
	if (((fdinfo->exflag & EF_UTF8) || Utf16) && (len & 0x1)) {
		_set_errno(EINVAL);
		return -1;
	}

	// Determine if we are reading UTF8
	if ((fdinfo->wxflag & WX_TEXT) && (fdinfo->exflag & EF_UTF8)) {
		return _ReadUtf8(fd, (wchar_t*)buffer, len);
	}

	// Now do the actual read of either binary or UTF16
	if (fdinfo->lookahead[0] != '\n' 
		|| StdioReadInternal(fd, BufferCursor, len, &BytesRead) == OsSuccess) {
		
		// Always check lookahead
		if (fdinfo->lookahead[0] != '\n') {
			BufferCursor[0] = fdinfo->lookahead[0];
			fdinfo->lookahead[0] = '\n';

			if (Utf16) {
				BufferCursor[1] = fdinfo->lookahead[1];
				fdinfo->lookahead[1] = '\n';
			}

			if (len > (1 + Utf16) && StdioReadInternal(fd, 
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
			fdinfo->wxflag |= WX_ATEOF;
		}
		else if (fdinfo->wxflag & WX_TEXT) {
			// Variables
			size_t i, j;

			// Detect reading newline
			if (BufferCursor[0] == '\n' && (!Utf16 || BufferCursor[1] == 0)) {
				fdinfo->wxflag |= WX_READNL;
			}
			else {
				fdinfo->wxflag &= ~WX_READNL;
			}

			for (i = 0, j = 0; i < BytesRead; i += 1 + Utf16)
			{
				// In text mode, a ctrl-z signals EOF
				if (BufferCursor[i] == 0x1a && (!Utf16 || BufferCursor[i + 1] == 0)) {
					fdinfo->wxflag |= WX_ATEOF;
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
					if (StdioReadInternal(fd, lookahead, 1 + Utf16, &count) == OsSuccess 
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

							if (fdinfo->wxflag & (WX_PIPE | WX_TTY)) {
								if (lookahead[0] == '\n' && (!Utf16 || !lookahead[1])) {
									BufferCursor[j++] = '\n';

									if (Utf16) {
										BufferCursor[j++] = 0;
									}
								}
								else {
									fdinfo->lookahead[0] = lookahead[0];
									fdinfo->lookahead[1] = lookahead[1];
								}
							}
							else {
								StdioSeekInternal(fd, -1 - Utf16, SEEK_CUR);
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

	// Done
	return (int)BytesRead;
}

/* fread
 * Reads an array of count elements, 
 * each one with a size of size bytes, from the stream and stores 
 * them in the block of memory specified by ptr. */
size_t fread(
	_In_ void *vptr, 
	_In_ size_t size, 
	_In_ size_t count, 
	_In_ FILE *stream)
{
	// Variables
	size_t rcnt = size * count;
	size_t read = 0;
	size_t pread = 0;

	// Sanitize parameters
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
		read += pcnt;
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
		if (!stream->_cnt && rcnt < BUFSIZ 
			&& (stream->_flag & (_IOMYBUF | _USERBUF))) {
			stream->_cnt = _read(stream->_fd, stream->_base, stream->_bufsiz);
			stream->_ptr = stream->_base;
			i = (stream->_cnt < rcnt) ? stream->_cnt : rcnt;

			/* If the buffer fill reaches eof but 
			 * fread wouldn't, clear eof. */
			if (i > 0 && i < stream->_cnt) {
				get_ioinfo(stream->_fd)->wxflag &= ~WX_ATEOF;
				stream->_flag &= ~_IOEOF;
			}
			
			if (i > 0) {
				memcpy(vptr, stream->_ptr, i);
				stream->_cnt -= i;
				stream->_ptr += i;
			}
		}
		else if (rcnt > INT_MAX) {
			i = _read(stream->_fd, vptr, INT_MAX);
		}
		else if (rcnt < BUFSIZ) {
			i = _read(stream->_fd, vptr, rcnt);
		}
		else {
			i = _read(stream->_fd, vptr, rcnt - BUFSIZ / 2);
		}

		// Update iterators
		pread += i;
		rcnt -= i;
		vptr = (char *)vptr + i;

		// Check for EOF condition
		// also for error conditions
		if (get_ioinfo(stream->_fd)->wxflag & WX_ATEOF) {
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
	read += pread;

	// Unlock file and return amount read
	_unlock_file(stream);
	return (read / size);
}
