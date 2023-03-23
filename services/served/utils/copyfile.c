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
 *
 */

#include <served/utils.h>
#include <stdio.h>
#include <stdlib.h>

#define _SEGMENT_SIZE (128 * 1024)

oserr_t CopyFile(const char* source, const char* destination)
{
    FILE*   sourceFile;
    FILE*   destinationFile;
    char*   buffer;
    oserr_t oserr = OS_EOK;

    sourceFile = fopen(source, "rb");
    if (!sourceFile) {
        return OS_ENOENT;
    }

    destinationFile = fopen(destination, "wb");
    if (!destinationFile) {
        fclose(sourceFile);
        return OS_EUNKNOWN;
    }

    buffer = (char*)malloc(_SEGMENT_SIZE);
    if (buffer == NULL) {
        fclose(sourceFile);
        fclose(destinationFile);
        return OS_EOOM;
    }

    while (1) {
        size_t read, written;

        read = fread(buffer, 1, _SEGMENT_SIZE, sourceFile);
        if (read == 0) {
            if (ferror(sourceFile)) {
                perror("CopyFile: failed to read source file");
                oserr = OS_EUNKNOWN;
            }
            break;
        }

        written = fwrite(buffer, 1, read, destinationFile);
        if (written != read) {
            oserr = OS_EUNKNOWN;
            break;
        }

        // was it last segment?
        if (read < _SEGMENT_SIZE) {
            break;
        }
    }

    free(buffer);
    fclose(sourceFile);
    fclose(destinationFile);
    return oserr;
}
