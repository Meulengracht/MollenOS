/**
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
 */

#ifndef __IO_H__
#define __IO_H__

#include <crtdefs.h>

#ifndef NAME_MAX
#define NAME_MAX 4096
#endif //!NAME_MAX

#define S_IXOTH         0x0001
#define S_IWOTH         0x0002
#define S_IROTH         0x0004
#define S_IRWXO         (S_IROTH | S_IWOTH | S_IXOTH)
#define S_IXGRP         0x0010
#define S_IWGRP         0x0020
#define S_IRGRP         0x0040
#define S_IRWXG         (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_IEXEC         0x0100
#define S_IWRITE        0x0200
#define S_IREAD         0x0400
#define S_IRWXU         (S_IREAD | S_IWRITE | S_IEXEC)
#define S_IFREG         0x1000
#define S_IFDIR         0x2000
#define S_IFLNK         0x4000

#define O_RDONLY        0x0001  /* open for reading only */
#define O_WRONLY        0x0002  /* open for writing only */
#define O_RDWR          0x0003  /* open for reading and writing */
#define O_APPEND        0x0008  /* writes done at eof */

#define O_CREAT         0x0100  /* create and open */
#define O_TRUNC         0x0200  /* open and truncate */
#define O_EXCL          0x0400  /* open only if doesn't already exist */
#define O_DIR           0x0800  /* create as a directory */

/* O_TEXT files have <cr><lf> sequences translated to <lf> on read()'s,
 ** and <lf> sequences translated to <cr><lf> on write()'s
 */
#define O_TEXT          0x4000  /* file mode is text (translated) */
#define O_BINARY        0x8000  /* file mode is binary (untranslated) */
#define O_WTEXT         0x10000 /* file mode is unicode */
#define O_U16TEXT       0x20000 /* file mode is UTF16 no BOM (translated) */
#define O_U8TEXT        0x40000 /* file mode is UTF8  no BOM (translated) */
#define O_NONBLOCK      0x80000  /* open in non-blocking mode */

/* macro to translate the C 2.0 name used to force binary mode for files */
#define O_RAW           O_BINARY

/* Open handle inherit bit */
#define O_NOINHERIT     0x0080  /* child process doesn't inherit file */

/* Temporary file bit - file is deleted when last handle is closed */
#define O_TMPFILE       0x0040  /* temporary file bit */

/* temporary access hint */
#define O_SHORT_LIVED   0x1000  /* temporary storage file, try not to flush */

/* directory access hint */
#define O_OBTAIN_DIR    0x2000  /* get information about a directory */

/* sequential/random access hints */
#define O_SEQUENTIAL    0x0020  /* file access is primarily sequential */
#define O_RANDOM        0x0010  /* file access is primarily random */

/**
 * @brief Types used in dirent::d_type. Most of these are here for completeness
 * and not neccessarily because they are used in Vali.
 */
#define DT_BLK     0 // This is a block device.
#define DT_CHR     1 // This is a character device.
#define DT_DIR     2 // This is a directory.
#define DT_FIFO    3 // This is a named pipe (FIFO).
#define DT_LNK     4 // This is a symbolic link.
#define DT_REG     5 // This is a regular file.
#define DT_SOCK    6 // This is a UNIX domain socket.
#define DT_UNKNOWN 7 // The file type could not be determined.

struct dirent {
    // d_ino is the file node ID. This is not consistent across boots
    // and is only consistent for the current boot.
    long d_ino;
    // d_off is index into the directory, and can be used with together
    // with seekdir().
    long d_off;
    // d_reclen is the length of the entire record. This may not match
    // the sizeof returned length.
    unsigned short d_reclen;
    // d_type is the type of the record converted to POSIX-comliant types.
    int d_type;
    // d_name is the name buffer and contains the name of the directory entry
    // up to NAME_MAX - 1. NAME_MAX is quite generous and should encompass most, but
    // will cut off entries longer than this. There is a byte reserved for the
    // NULL terminator.
    char d_name[NAME_MAX];
};
typedef struct dirent dirent;
typedef struct DIR DIR;

_CODE_BEGIN
// shared io interface
CRTDECL(int, marktty(int iod));
CRTDECL(int, pipe(long size, int flags));
CRTDECL(int, dup(int iod));
CRTDECL(int, open(const char *file, int flags, ...));
CRTDECL(int, close(int fd));
CRTDECL(int, read(int fd, void *buffer, unsigned int len));
CRTDECL(int, write(int fd, const void *buffer, unsigned int length));

// file interface
CRTDECL(long,      lseek(int fd, long offset, int whence));
CRTDECL(long long, lseeki64(int fd, long long offset, int whence));
CRTDECL(long,      tell(int fd));
CRTDECL(long long, telli64(int fd));
CRTDECL(int,       chsize(int fd, long size));

// directory interface
CRTDECL(int,            mkdir(const char* path, unsigned int mode));
CRTDECL(struct DIR*,    opendir(const char* path));
CRTDECL(int,            closedir(struct DIR* dir));
CRTDECL(struct dirent*, readdir(struct DIR* dir));
CRTDECL(int,            rewinddir(struct DIR* dir));
CRTDECL(int,            seekdir(struct DIR* dir, long index));
CRTDECL(long,           telldir(struct DIR* dir));

// file and directory interface
CRTDECL(int, link(const char *from, const char *to, int symbolic));
CRTDECL(int, unlink(const char *path));
CRTDECL(int, isatty(int fd));
_CODE_END

#endif // !__IO_H__
