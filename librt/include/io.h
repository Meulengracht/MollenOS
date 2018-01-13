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
 * MollenOS - Standard C Library (Legacy IO)
 *  - Contains the implementation of the io interface
 */
#ifndef __STDC_IO__
#define __STDC_IO__

/* Includes 
 * - Library */
#include <os/osdefs.h>

#define _S_IREAD		0x0001 
#define _S_IWRITE		0x0002

#define _O_RDONLY       0x0000  /* open for reading only */
#define _O_WRONLY       0x0001  /* open for writing only */
#define _O_RDWR         0x0002  /* open for reading and writing */
#define _O_APPEND       0x0008  /* writes done at eof */

#define _O_CREAT        0x0100  /* create and open */
#define _O_TRUNC        0x0200  /* open and truncate */
#define _O_EXCL         0x0400  /* open only if doesn't already exist */

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

/* In case of non-strict ANSI standard c 
 * we should define 'shortcut' defines for some of the functions. */
#if !__STDC__
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
#endif  // !__STDC__

/* Directory handling support. 
 * Structures contain basic information about the directory
 * and it's entries. */
struct directory_handle {
    UUId_t d_handle;
    int    d_index;
};
struct directory_entry {
    Flags_t d_type;
    char    d_name[256];
};

_CODE_BEGIN
CRTDECL(Flags_t,    _faccess(int oflags));
CRTDECL(Flags_t,    _fopts(int oflags));
CRTDECL(int,        _fval(int ocode));

// file interface
CRTDECL(int,        _open(const char *file, int flags, ...));
CRTDECL(int,        _close(int fd));
CRTDECL(int,        _read(int fd, void *buffer, unsigned int len));
CRTDECL(int,        _write(int fd, void *buffer, unsigned int length));
CRTDECL(long,       _lseek(int fd, long offset, int whence));
CRTDECL(long long,  _lseeki64(int fd, long long offset, int whence));
CRTDECL(long,       _tell(int fd));
CRTDECL(long long,  _telli64(int fd));
CRTDECL(int,        _chsize(int fd, long size));

// directory interface
CRTDECL(int,        _opendir(const char *path, int flags, struct directory_handle **handle));
CRTDECL(int,        _closedir(struct directory_handle *handle));
CRTDECL(int,        _readdir(struct directory_handle *handle, struct directory_entry *entry));

// shared interface
CRTDECL(int,        _link(const char *from, const char *to, int symbolic));
CRTDECL(int,        _unlink(const char *path));
CRTDECL(int,        _isatty(int fd));
_CODE_END

/* In case of non-strict ANSI standard c 
 * we should define 'shortcut' defines for some of the functions. */
#if !__STDC__
#define open _open
#define close _close
#define read _read
#define write _write
#define lseek _lseek
#define tell _tell
#define unlink _unlink
#define isatty _isatty
#endif // !__STDC__
#endif // !__STDC_IO__
