/* MollenOS
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
 *    - Flag domain conversions
 */
//#define __TRACE

#include <ddk/utils.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <io.h>
#include <internal/_io.h>

int
_fflags(
    _In_  const char* mode, 
    _Out_ int*        open_flags, 
    _Out_ int*        stream_flags)
{
    int plus = strchr(mode, '+') != NULL;
    TRACE("_fflags(%s)", mode);

    // Skip leading whitespaces
    while (*mode == ' ') {
        mode++;
    }

    // Handle 'r', 'w' and 'a'
    switch (*mode++) {
        case 'R':
        case 'r':
            *open_flags = plus ? O_RDWR : O_RDONLY;
            *stream_flags = plus ? _IORW : _IOREAD;
            break;
        case 'W':
        case 'w':
            *open_flags = O_CREAT | O_TRUNC | (plus ? O_RDWR : O_WRONLY);
            *stream_flags = plus ? _IORW : _IOWRT;
            break;
        case 'A':
        case 'a':
            *open_flags = O_CREAT | O_APPEND | (plus ? O_RDWR : O_WRONLY);
            *stream_flags = plus ? _IORW : _IOWRT;
            break;
        default:
            _set_errno(EINVAL);
            return -1;
    }

    // Now handle all the other options for opening
    // like text, binary, file-type
    while (*mode && *mode != ',') {
        switch (*mode++) {
            case 'B':
            case 'b':
                *open_flags |= O_BINARY;
                *open_flags &= ~O_TEXT;
                break;
            case 't':
                *open_flags |= O_TEXT;
                *open_flags &= ~O_BINARY;
                break;
            case 'D':
                *open_flags |= O_TEMPORARY;
                break;
            case 'T':
                *open_flags |= O_SHORT_LIVED;
                break;
            case 'c':
                *stream_flags |= _IOCOMMIT;
                break;
            case 'n':
                *stream_flags &= ~_IOCOMMIT;
                break;
            case 'N':
                *open_flags |= O_NOINHERIT;
                break;
            case '+':
            case ' ':
            case 'a':
            case 'w':
                break;
            case 'S':
            case 'R':
                TRACE("ignoring cache optimization flag: %c\n", mode[-1]);
                break;
            default:
                ERROR("incorrect mode flag: %c\n", mode[-1]);
                break;
        }
    }

    // Now handle text-formatting options
    if (*mode == ',') {
        static const char ccs[] = {'c', 'c', 's'};
        static const char utf8[] = {'u', 't', 'f', '-', '8'};
        static const char utf16le[] = {'u', 't', 'f', '-', '1', '6', 'l', 'e'};
        static const char unicode[] = {'u', 'n', 'i', 'c', 'o', 'd', 'e'};

        mode++;
        while (*mode == ' ')
            mode++;
        if (strncmp(ccs, mode, sizeof(ccs) / sizeof(ccs[0])))
            return -1;
        mode += sizeof(ccs) / sizeof(ccs[0]);
        while (*mode == ' ')
            mode++;
        if (*mode != '=')
            return -1;
        mode++;
        while (*mode == ' ')
            mode++;

        if (!strncasecmp(utf8, mode, sizeof(utf8) / sizeof(utf8[0])))
        {
            *open_flags |= O_U8TEXT;
            mode += sizeof(utf8) / sizeof(utf8[0]);
        }
        else if (!strncasecmp(utf16le, mode, sizeof(utf16le) / sizeof(utf16le[0])))
        {
            *open_flags |= O_U16TEXT;
            mode += sizeof(utf16le) / sizeof(utf16le[0]);
        }
        else if (!strncasecmp(unicode, mode, sizeof(unicode) / sizeof(unicode[0])))
        {
            *open_flags |= O_WTEXT;
            mode += sizeof(unicode) / sizeof(unicode[0]);
        }
        else {
            _set_errno(EINVAL);
            return -1;
        }

        // Skip spaces
        while (*mode == ' ') {
            mode++;
        }
    }

    // We should be at end of string, otherwise error
    if (*mode != 0) {
        return -1;
    }
    return 0;
}

/* _faccess
 * Converts the ANSI-C-mode flags into our access flags */
Flags_t
_faccess(
    _In_ int oflags)
{
    Flags_t mFlags = __FILE_READ_ACCESS;

    // Convert to access flags
    if (oflags & O_WRONLY) {
        mFlags = __FILE_WRITE_ACCESS;
    }
    if (oflags & O_RDWR) {
        mFlags = __FILE_READ_ACCESS | __FILE_WRITE_ACCESS;
    }
    return mFlags;
}

/* _fopts
 * Converts the ANSI-C-mode flags into our option flags */
Flags_t
_fopts(
    _In_ int oflags)
{
    Flags_t mFlags = 0;
    TRACE("_fopts(0x%x)", oflags);

    // Take care of opening flags
    if (oflags & O_CREAT) {
        mFlags |= __FILE_CREATE;
        if (oflags & O_RECURS) {
            mFlags |= __FILE_CREATE_RECURSIVE;
        }
        if (oflags & O_DIR) {
            mFlags |= __FILE_DIRECTORY;
        }
    }
    if (oflags & O_TRUNC) {
        mFlags |= __FILE_TRUNCATE;
    }
    if (oflags & O_EXCL) {
        mFlags |= __FILE_FAILONEXIST;
    }
    if (oflags & O_TEMPORARY) {
        mFlags |= __FILE_TEMPORARY;
    }
    if (oflags & O_BINARY) {
        mFlags |= __FILE_BINARY;
    }
    return mFlags;
}
