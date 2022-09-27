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

#include <served/state.h>
#include <stdio.h>
#include <stdlib.h>

static oserr_t __ReadState(const char* path, void** bufferOut, size_t* bufferSize)
{
    FILE*   stateFile = fopen(path, "r");
    oserr_t oserr     = OsOK;
    if (stateFile == NULL) {
        return OsNotExists;
    }

    fseek(stateFile, 0, SEEK_END);
    long size = ftell(stateFile);
    if (size == 0) {
        oserr = OsIncomplete;
        goto exit;
    }

    void* data = malloc(size);
    if (data == NULL) {
        oserr = OsOutOfMemory;
        goto exit;
    }

    size_t read = fread(data, 1, size, stateFile);
    if (read != (size_t)size) {
        oserr = OsIncomplete;
        goto exit;
    }

    *bufferOut = data;
    *bufferSize = read;

exit:
    fclose(stateFile);
    return oserr;
}

oserr_t StateLoad(void)
{
    void*   stateData;
    size_t  stateDataLength;
    oserr_t oserr;

    oserr = __ReadState("/data/served/state.json", &stateData, &stateDataLength);
    if (oserr != OsOK) {
        return oserr;
    }
}

oserr_t StateInitialize(void)
{

}
