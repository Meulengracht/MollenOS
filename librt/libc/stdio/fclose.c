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
 */

#include <io.h>
#include <internal/_io.h>
#include <internal/_file.h>
#include <stdlib.h>
#include <string.h>

static void
__FILE_Delete(
        _In_ FILE *stream)
{
    free(stream->_tmpfname);
    if (stream->Flags & _IOMYBUF) {
        free(stream->_base);
    }
    free(stream);
}

int fclose(FILE *stream)
{
	int iod;

	flockfile(stream);
	if (__FILE_ShouldFlush(stream)) {
		fflush(stream);
	}

    // After flushing, we now cleanup the stream. We start
    // out by protecting the underlying IO descriptor by zeroing
    // it, but then unlock the stream
    iod = stream->IOD;
    stream->IOD = -1;
    funlockfile(stream);
    __FILE_Delete(stream);

    // Now close the underlying file-descriptor
	return close(iod);
}
