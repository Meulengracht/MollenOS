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
 * MollenOS C Library - OS-File Utilities
 */

/* Includes
 * - System */
#include <os/utils.h>
#include <os/file.h>
#include <os/syscall.h>

/* Includes 
 * - Library */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "local.h"

int _fflags(
	_In_ __CONST char *mode, 
	_In_ int *open_flags, 
	_In_ int *stream_flags)
{
	// Variables
	int plus = strchr(mode, '+') != NULL;

	// Skip leading whitespaces
	while (*mode == ' ') {
		mode++;
	}

	// Handle 'r', 'w' and 'a'
	switch (*mode++) {
		case 'R':
		case 'r':
			*open_flags = plus ? _O_RDWR : _O_RDONLY;
			*stream_flags = plus ? _IORW : _IOREAD;
			break;
		case 'W':
		case 'w':
			*open_flags = _O_CREAT | _O_TRUNC | (plus ? _O_RDWR : _O_WRONLY);
			*stream_flags = plus ? _IORW : _IOWRT;
			break;
		case 'A':
		case 'a':
			*open_flags = _O_CREAT | _O_APPEND | (plus ? _O_RDWR : _O_WRONLY);
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
				*open_flags |= _O_BINARY;
				*open_flags &= ~_O_TEXT;
				break;
			case 't':
				*open_flags |= _O_TEXT;
				*open_flags &= ~_O_BINARY;
				break;
			case 'D':
				*open_flags |= _O_TEMPORARY;
				break;
			case 'T':
				*open_flags |= _O_SHORT_LIVED;
				break;
			case 'c':
				*stream_flags |= _IOCOMMIT;
				break;
			case 'n':
				*stream_flags &= ~_IOCOMMIT;
				break;
			case 'N':
				*open_flags |= _O_NOINHERIT;
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
			*open_flags |= _O_U8TEXT;
			mode += sizeof(utf8) / sizeof(utf8[0]);
		}
		else if (!strncasecmp(utf16le, mode, sizeof(utf16le) / sizeof(utf16le[0])))
		{
			*open_flags |= _O_U16TEXT;
			mode += sizeof(utf16le) / sizeof(utf16le[0]);
		}
		else if (!strncasecmp(unicode, mode, sizeof(unicode) / sizeof(unicode[0])))
		{
			*open_flags |= _O_WTEXT;
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
Flags_t _faccess(
	int oflags)
{
	// Variables
	Flags_t mFlags = __FILE_READ_ACCESS;

	// Convert to access flags
	if (oflags & _O_WRONLY) {
		mFlags = __FILE_WRITE_ACCESS;
	}
	if (oflags & _O_RDWR) {
		mFlags |= __FILE_READ_ACCESS | __FILE_WRITE_ACCESS;
	}
	return mFlags;
}

/* _fopts
 * Converts the ANSI-C-mode flags into our option flags */
Flags_t _fopts(
	int oflags)
{
	// Variables
	Flags_t mFlags = 0;

	// Take care of opening flags
	if (oflags & _O_CREAT) {
		mFlags |= __FILE_CREATE;
	}
	if (oflags & _O_TRUNC) {
		mFlags |= __FILE_TRUNCATE;
	}
	if (oflags & _O_EXCL) {
		mFlags |= __FILE_FAILONEXIST;
	}
	if (oflags & _O_TEMPORARY) {
		mFlags |= __FILE_TEMPORARY;
	}
	if (oflags & _O_BINARY) {
		mFlags |= __FILE_BINARY;
	}
	return mFlags;
}

/* _fval 
 * Validates a vfs response and converts it to errno */
int _fval(
	int ocode)
{
	if (ocode == 0)
		_set_errno(EOK);
	else if (ocode == 1)
		_set_errno(EINVAL);
	else if (ocode == 2)
		_set_errno(EINVAL);
	else if (ocode == 3)
		_set_errno(ENOENT);
	else if (ocode == 4)
		_set_errno(ENOENT);
	else if (ocode == 5)
		_set_errno(EACCES);
	else if (ocode == 6)
		_set_errno(EISDIR);
	else if (ocode == 7)
		_set_errno(EEXIST);
	else if (ocode == 8)
		_set_errno(EIO);
	else
		_set_errno(EINVAL);
	return errno;
}
