/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS C Library - Legacy IO
*/

#ifndef __ANSI_IO_INC__
#define __ANSI_IO_INC__

/* Includes */
#include <os/osdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************
 *        Definitions          *
 *******************************/

#define _S_IREAD		0x0001 
#define _S_IWRITE		0x0002

#define _O_RDONLY       0x0000  /* open for reading only */
#define _O_WRONLY       0x0001  /* open for writing only */
#define _O_RDWR         0x0002  /* open for reading and writing */
#define _O_APPEND       0x0008  /* writes done at eof */

#define _O_CREAT        0x0100  /* create and open file */
#define _O_TRUNC        0x0200  /* open and truncate */
#define _O_EXCL         0x0400  /* open only if file doesn't already exist */

/* O_TEXT files have <cr><lf> sequences translated to <lf> on read()'s,
 ** and <lf> sequences translated to <cr><lf> on write()'s
 */

#define _O_TEXT         0x4000  /* file mode is text (translated) */
#define _O_BINARY       0x8000  /* file mode is binary (untranslated) */
#define _O_WTEXT        0x10000 /* file mode is UTF16 (translated) */
#define _O_U16TEXT      0x20000 /* file mode is UTF16 no BOM (translated) */
#define _O_U8TEXT       0x40000 /* file mode is UTF8  no BOM (translated) */

/* macro to translate the C 2.0 name used to force binary mode for files */

#define _O_RAW  _O_BINARY

/* Open handle inherit bit */

#define _O_NOINHERIT    0x0080  /* child process doesn't inherit file */

/* Temporary file bit - file is deleted when last handle is closed */

#define _O_TEMPORARY    0x0040  /* temporary file bit */

/* temporary access hint */

#define _O_SHORT_LIVED  0x1000  /* temporary storage file, try not to flush */

/* directory access hint */

#define _O_OBTAIN_DIR   0x2000  /* get information about a directory */

/* sequential/random access hints */

#define _O_SEQUENTIAL   0x0020  /* file access is primarily sequential */
#define _O_RANDOM       0x0010  /* file access is primarily random */

#if !__STDC__
/* Non-ANSI names for compatibility */
#define O_RDONLY        _O_RDONLY
#define O_WRONLY        _O_WRONLY
#define O_RDWR          _O_RDWR
#define O_APPEND        _O_APPEND
#define O_CREAT         _O_CREAT
#define O_TRUNC         _O_TRUNC
#define O_EXCL          _O_EXCL
#define O_TEXT          _O_TEXT
#define O_BINARY        _O_BINARY
#define O_RAW           _O_BINARY
#define O_TEMPORARY     _O_TEMPORARY
#define O_NOINHERIT     _O_NOINHERIT
#define O_SEQUENTIAL    _O_SEQUENTIAL
#define O_RANDOM        _O_RANDOM
#endif  /* !__STDC__ */

/*******************************
 *       File Access           *
 *******************************/
_CRTIMP Flags_t _faccess(int oflags);
_CRTIMP Flags_t _fopts(int oflags);
_CRTIMP int _fval(int ocode);
_CRTIMP int _open(
	_In_ __CONST char *file, 
	_In_ int flags,
	...);
_CRTIMP int _close(int fd);

_CRTIMP int _read(int fd, void *buffer, unsigned int len);
_CRTIMP int _write(
	_In_ int fd, 
	_In_ void *buffer, 
	_In_ unsigned int length);

_CRTIMP long _lseek(
	_In_ int fd, 
	_In_ long offset,
	_In_ int whence);
_CRTIMP long long _lseeki64(
	_In_ int fd, 
	_In_ long long offset, 
	_In_ int whence);
_CRTIMP long long _telli64(
	_In_ int fd);
_CRTIMP long _tell(int fd);
_CRTIMP int _unlink(__CONST char *filename);

_CRTIMP int _isatty(int fd);

#if !__STDC__
/* Alias functions */
#define open _open
#define close _close
#define read _read
#define write _write
#define lseek _lseek
#define tell _tell
#define unlink _unlink
#endif

#ifdef __cplusplus
}
#endif

#endif
