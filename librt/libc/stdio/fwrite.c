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
 * MollenOS C Library - Write to file-handles
 */

/* Includes
 * - System */
#include <os/driver/file.h>
#include <os/syscall.h>
#include <os/thread.h>

/* Includes 
 * - Library */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "local.h"

/* _write
 * This is the ANSI C version of fwrite */
int _write(int fd, void *buffer, unsigned int length)
{
	DWORD num_written;
	ioinfo *info = get_ioinfo(fd);
	HANDLE hand = info->handle;

/* Don't trace small writes, it gets *very* annoying */
#if 0
    if (count > 32)
        TRACE(":fd (%d) handle (%d) buf (%p) len (%d)\n",fd,hand,buf,count);
#endif
	if (hand == INVALID_HANDLE_VALUE)
	{
		*_errno() = EBADF;
		return -1;
	}

	if (((info->exflag & EF_UTF8) || (info->exflag & EF_UTF16)) && count & 1)
	{
		*_errno() = EINVAL;
		return -1;
	}

	/* If appending, go to EOF */
	if (info->wxflag & WX_APPEND)
		_lseek(fd, 0, FILE_END);

	if (!(info->wxflag & WX_TEXT))
	{
		if (WriteFile(hand, buf, count, &num_written, NULL) && (num_written == count))
			return num_written;
		TRACE("WriteFile (fd %d, hand %p) failed-last error (%d)\n", fd,
			  hand, GetLastError());
		*_errno() = ENOSPC;
	}
	else
	{
		unsigned int i, j, nr_lf, size;
		char *p = NULL;
		const char *q;
		const char *s = buf, *buf_start = buf;

		if (!(info->exflag & (EF_UTF8 | EF_UTF16)))
		{
			/* find number of \n */
			for (nr_lf = 0, i = 0; i < count; i++)
				if (s[i] == '\n')
					nr_lf++;
			if (nr_lf)
			{
				size = count + nr_lf;
				if ((q = p = malloc(size)))
				{
					for (s = buf, i = 0, j = 0; i < count; i++)
					{
						if (s[i] == '\n')
							p[j++] = '\r';
						p[j++] = s[i];
					}
				}
				else
				{
					FIXME("Malloc failed\n");
					nr_lf = 0;
					size = count;
					q = buf;
				}
			}
			else
			{
				size = count;
				q = buf;
			}
		}
		else if (info->exflag & EF_UTF16)
		{
			for (nr_lf = 0, i = 0; i < count; i += 2)
				if (s[i] == '\n' && s[i + 1] == 0)
					nr_lf += 2;
			if (nr_lf)
			{
				size = count + nr_lf;
				if ((q = p = malloc(size)))
				{
					for (s = buf, i = 0, j = 0; i < count; i++)
					{
						if (s[i] == '\n' && s[i + 1] == 0)
						{
							p[j++] = '\r';
							p[j++] = 0;
						}
						p[j++] = s[i++];
						p[j++] = s[i];
					}
				}
				else
				{
					FIXME("Malloc failed\n");
					nr_lf = 0;
					size = count;
					q = buf;
				}
			}
			else
			{
				size = count;
				q = buf;
			}
		}
		else
		{
			DWORD conv_len;

			for (nr_lf = 0, i = 0; i < count; i += 2)
				if (s[i] == '\n' && s[i + 1] == 0)
					nr_lf++;

			conv_len = WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)buf, count / 2, NULL, 0, NULL, NULL);
			if (!conv_len)
			{
				_dosmaperr(GetLastError());
				free(p);
				return -1;
			}

			size = conv_len + nr_lf;
			if ((p = malloc(count + nr_lf * 2 + size)))
			{
				for (s = buf, i = 0, j = 0; i < count; i++)
				{
					if (s[i] == '\n' && s[i + 1] == 0)
					{
						p[j++] = '\r';
						p[j++] = 0;
					}
					p[j++] = s[i++];
					p[j++] = s[i];
				}
				q = p + count + nr_lf * 2;
				WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)p, count / 2 + nr_lf,
									p + count + nr_lf * 2, conv_len + nr_lf, NULL, NULL);
			}
			else
			{
				FIXME("Malloc failed\n");
				nr_lf = 0;
				size = count;
				q = buf;
			}
		}

		if (!WriteFile(hand, q, size, &num_written, NULL))
			num_written = -1;
		if (p)
			free(p);
		if (num_written != size)
		{
			TRACE("WriteFile (fd %d, hand %p) failed-last error (%d), num_written %d\n",
				  fd, hand, GetLastError(), num_written);
			*_errno() = ENOSPC;
			return s - buf_start;
		}
		return count;
	}

	return -1;
}

/* The fwrite
* writes to a file handle */
size_t fwrite(
	_In_ __CONST void *vptr,
	_In_ size_t size,
	_In_ size_t count,
	_In_ FILE *stream)
{
	// Variables
	size_t wrcnt = size * count;
	int written = 0;
	if (size == 0)
		return 0;

	_lock_file(stream);

	while (wrcnt)
	{
		if (stream->_cnt < 0)
		{
			stream->_flag |= _IOERR;
			break;
		}
		else if (stream->_cnt)
		{
			int pcnt = (stream->_cnt > wrcnt) ? wrcnt : stream->_cnt;
			memcpy(stream->_ptr, vptr, pcnt);
			stream->_cnt -= pcnt;
			stream->_ptr += pcnt;
			written += pcnt;
			wrcnt -= pcnt;
			vptr = (const char *)vptr + pcnt;
		}
		else if ((stream->_flag & _IONBF) || ((stream->_flag & (_IOMYBUF | _USERBUF)) && wrcnt >= file->_bufsiz) || (!(file->_flag & (_IOMYBUF | _USERBUF)) && wrcnt >= MSVCRT_INTERNAL_BUFSIZ))
		{
			size_t pcnt;
			int bufsiz;

			if (stream->_flag & _IONBF)
				bufsiz = 1;
			else if (!(stream->_flag & (_IOMYBUF | _USERBUF)))
				bufsiz = INTERNAL_BUFSIZ;
			else
				bufsiz = stream->_bufsiz;

			pcnt = (wrcnt / bufsiz) * bufsiz;

			if (os_flush_buffer(stream) == EOF)
				break;

			if (_write(stream->_fd, vptr, pcnt) <= 0)
			{
				stream->_flag |= _IOERR;
				break;
			}
			written += pcnt;
			wrcnt -= pcnt;
			vptr = (const char *)vptr + pcnt;
		}
		else
		{
			if (_flsbuf(*(const char *)vptr, stream) == EOF)
				break;
			written++;
			wrcnt--;
			vptr = (const char *)vptr + 1;
		}
	}

	_unlock_file(stream);
	return written / size;
}
