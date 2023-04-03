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

#include <assert.h>
#include <errno.h>
#include <internal/_file.h>
#include <io.h>
#include <stdio.h>

long tell(
	_In_ int fd)
{
	return lseek(fd, 0, SEEK_CUR);
}

long long telli64(
	_In_ int fd)
{
	return lseeki64(fd, 0, SEEK_CUR);
}

static long long
__BufferedPosition(
        _In_ FILE*     stream,
        _In_ long long base)
{
    return base + __FILE_BufferPosition(stream);
}

long long
ftelli64(
	_In_ FILE *stream)
{
	long long position;

	if (!stream) {
		_set_errno(EINVAL);
		return -1LL;
	}
	
	flockfile(stream);

	position = telli64(stream->IOD);
	if (position == -1) {
		funlockfile(stream);
		return -1;
	}

    // If the stream is buffered, we need to correct for buffer
    // position.
    if (__FILE_IsBuffered(stream)) {
        position = __BufferedPosition(stream, position);
    }
    funlockfile(stream);
    return position;
}

off_t ftello(
	_In_ FILE *stream)
{
	return (off_t)ftelli64(stream);
}

long ftell(
	_In_ FILE* stream)
{
	off_t offset = ftello(stream);
	if ((long)offset != offset) {
		_set_errno(EOVERFLOW);
		return -1L;
	}
	return offset;
}
