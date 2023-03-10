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
//#define __TRACE

#include <ddk/utils.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <internal/_io.h>
#include <os/services/file.h>

static int
__HandleAccess(
        _In_  const char* mode,
        _In_  bool        plus,
        _Out_ int*        flagsOut)
{
    // The first character of the mode must be either of
    // 'r', 'w' and 'a'
    switch (*mode++) {
        case 'R':
        case 'r':
            *flagsOut = O_TEXT | (plus ? O_RDWR : O_RDONLY);
            break;
        case 'W':
        case 'w':
            *flagsOut = O_CREAT | O_TRUNC | O_TEXT | (plus ? O_RDWR : O_WRONLY);
            break;
        case 'A':
        case 'a':
            *flagsOut = O_CREAT | O_APPEND | O_TEXT | (plus ? O_RDWR : O_WRONLY);
            break;
        default:
            _set_errno(EINVAL);
            return -1;
    }
    return 0;
}

int
__fmode_to_flags(
        _In_  const char* mode,
        _Out_ int*        flagsOut)
{
    bool plus = strchr(mode, '+') != NULL;

    // Skip leading whitespaces
    while (*mode == ' ') {
        mode++;
    }

    // Handle the access type, it *must* be set
    if (__HandleAccess(mode, plus, flagsOut)) {
        return -1;
    }

    // Now handle all the other options for opening
    // like text, binary, file-type
    while (*mode && *mode != ',') {
        switch (*mode++) {
            case 'B':
            case 'b':
                *flagsOut |= O_BINARY;
                *flagsOut &= ~O_TEXT;
                break;
            case 't':
                *flagsOut |= O_TEXT;
                *flagsOut &= ~O_BINARY;
                break;
            case 'D':
                *flagsOut |= O_TMPFILE;
                break;
            case 'T':
                *flagsOut |= O_SHORT_LIVED;
                break;
            case 'N':
                *flagsOut |= O_NOINHERIT;
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
        if (strncmp(ccs, mode, sizeof(ccs) / sizeof(ccs[0])) != 0)
            return -1;
        mode += sizeof(ccs) / sizeof(ccs[0]);
        while (*mode == ' ')
            mode++;
        if (*mode != '=')
            return -1;
        mode++;
        while (*mode == ' ')
            mode++;

        if (!strncasecmp(utf8, mode, sizeof(utf8) / sizeof(utf8[0]))) {
            *flagsOut |= O_U8TEXT;
            mode += sizeof(utf8) / sizeof(utf8[0]);
        } else if (!strncasecmp(utf16le, mode, sizeof(utf16le) / sizeof(utf16le[0]))) {
            *flagsOut |= O_U16TEXT;
            mode += sizeof(utf16le) / sizeof(utf16le[0]);
        } else if (!strncasecmp(unicode, mode, sizeof(unicode) / sizeof(unicode[0]))) {
            *flagsOut |= O_WTEXT;
            mode += sizeof(unicode) / sizeof(unicode[0]);
        } else {
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
unsigned int
_faccess(
    _In_ int oflags)
{
    unsigned int mFlags = __FILE_READ_ACCESS;

    // Convert to access flags
    if (oflags & O_WRONLY) {
        mFlags = __FILE_WRITE_ACCESS;
    }
    if (oflags & O_RDWR) {
        mFlags = __FILE_READ_ACCESS | __FILE_WRITE_ACCESS;
    }
    return mFlags;
}

unsigned int _fperms(unsigned int mode)
{
    unsigned int permissions = 0;
    if (mode & S_IXOTH) permissions |= FILE_PERMISSION_OTHER_EXECUTE;
    if (mode & S_IWOTH) permissions |= FILE_PERMISSION_OTHER_WRITE;
    if (mode & S_IROTH) permissions |= FILE_PERMISSION_OTHER_READ;
    if (mode & S_IXGRP) permissions |= FILE_PERMISSION_GROUP_EXECUTE;
    if (mode & S_IWGRP) permissions |= FILE_PERMISSION_GROUP_WRITE;
    if (mode & S_IRGRP) permissions |= FILE_PERMISSION_GROUP_READ;
    if (mode & S_IEXEC) permissions |= FILE_PERMISSION_OWNER_EXECUTE;
    if (mode & S_IWRITE) permissions |= FILE_PERMISSION_OWNER_WRITE;
    if (mode & S_IREAD) permissions |= FILE_PERMISSION_OWNER_READ;
    return permissions;
}

unsigned int
_fopts(
    _In_ int oflags)
{
    unsigned int mFlags = 0;
    TRACE("_fopts(0x%x)", oflags);

    // Take care of opening flags
    if (oflags & O_CREAT) {
        mFlags |= __FILE_CREATE;
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
    if (oflags & O_TMPFILE) {
        mFlags |= __FILE_TEMPORARY;
    }
    if (oflags & O_BINARY) {
        mFlags |= __FILE_BINARY;
    }
    return mFlags;
}
