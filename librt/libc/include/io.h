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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic IO Operations. These are functions that may be supported by all
 * io descriptors. If they are not supported errno is set to ENOTSUPP
 */

#ifndef __IO_H__
#define __IO_H__

#include <os/osdefs.h>

#define S_IREAD         0x0001 
#define S_IWRITE        0x0002

#define O_RDONLY        0x0000  /* open for reading only */
#define O_WRONLY        0x0001  /* open for writing only */
#define O_RDWR          0x0002  /* open for reading and writing */
#define O_APPEND        0x0008  /* writes done at eof */

#define O_CREAT         0x0100  /* create and open */
#define O_TRUNC         0x0200  /* open and truncate */
#define O_EXCL          0x0400  /* open only if doesn't already exist */
#define O_RECURS        0x0800  /* create missing path compononents */
#define O_DIR           0x1000  /* create as a directory */

/* O_TEXT files have <cr><lf> sequences translated to <lf> on read()'s,
 ** and <lf> sequences translated to <cr><lf> on write()'s
 */
#define O_TEXT          0x4000  /* file mode is text (translated) */
#define O_BINARY        0x8000  /* file mode is binary (untranslated) */
#define O_WTEXT         0x10000 /* file mode is unicode */
#define O_U16TEXT       0x20000 /* file mode is UTF16 no BOM (translated) */
#define O_U8TEXT        0x40000 /* file mode is UTF8  no BOM (translated) */

/* macro to translate the C 2.0 name used to force binary mode for files */
#define O_RAW           O_BINARY

/* Open handle inherit bit */
#define O_NOINHERIT     0x0080  /* child process doesn't inherit file */

/* Temporary file bit - file is deleted when last handle is closed */
#define O_TEMPORARY     0x0040  /* temporary file bit */

/* temporary access hint */
#define O_SHORT_LIVED   0x1000  /* temporary storage file, try not to flush */

/* directory access hint */
#define O_OBTAIN_DIR    0x2000  /* get information about a directory */

/* sequential/random access hints */
#define O_SEQUENTIAL    0x0020  /* file access is primarily sequential */
#define O_RANDOM        0x0010  /* file access is primarily random */

struct DIR {
    int d_handle;
    int d_index;
};
struct DIRENT {
    unsigned int d_options;
    unsigned int d_perms;
    char         d_name[256];
};

_CODE_BEGIN
// shared io interface
CRTDECL(int,        marktty(int iod));
CRTDECL(int,        pipe(long size, int flags));
CRTDECL(int,        dup(int iod));
CRTDECL(int,        open(const char *file, int flags, ...));
CRTDECL(int,        close(int fd));
CRTDECL(int,        read(int fd, void *buffer, unsigned int len));
CRTDECL(int,        write(int fd, const void *buffer, unsigned int length));
CRTDECL(int,        iolock(int fd));
CRTDECL(int,        iounlock(int fd));

// file interface
CRTDECL(long,       lseek(int fd, long offset, int whence));
CRTDECL(long long,  lseeki64(int fd, long long offset, int whence));
CRTDECL(long,       tell(int fd));
CRTDECL(long long,  telli64(int fd));
CRTDECL(int,        chsize(int fd, long size));

// directory interface
CRTDECL(int,        mkdir(const char *path, int mode));
CRTDECL(int,        opendir(const char *path, int flags, struct DIR **handle));
CRTDECL(int,        closedir(struct DIR *handle));
CRTDECL(int,        readdir(struct DIR *handle, struct DIRENT *entry));

// file and directory interface
CRTDECL(int,        link(const char *from, const char *to, int symbolic));
CRTDECL(int,        unlink(const char *path));
CRTDECL(int,        isatty(int fd));
_CODE_END

#endif // !__IO_H__
