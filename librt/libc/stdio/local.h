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
 * MollenOS - C Standard Library
 * - Standard IO Support header
 */

#ifndef __STDIO_SUPPORT_H__
#define __STDIO_SUPPORT_H__

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>

/* values for wxflag in file descriptor */
#define WX_OPEN             0x01
#define WX_ATEOF            0x02
#define WX_READNL           0x04  /* read started with \n */
#define WX_READEOF          0x04  /* like ATEOF, but for underlying file rather than buffer */
#define WX_PIPE             0x08
#define WX_READCR           0x08  /* underlying file is at \r */
#define WX_DONTINHERIT      0x10
#define WX_APPEND           0x20
#define WX_TTY              0x40
#define WX_TEXT             0x80

/* values for exflag - it's used differently in msvcr90.dll*/
#define EF_UTF8             0x01
#define EF_UTF16            0x02
#define EF_UNK_UNICODE      0x08

#define INTERNAL_BUFSIZ     4096
#define INTERNAL_MAXFILES   1024

typedef struct {
    UUId_t              handle;
    unsigned char       wxflag;
    char                lookahead[3];
    int                 exflag;
    void               *file;
    Spinlock_t          lock;
} ioobject;

__EXTERN ioobject* get_ioinfo(int fd);
__EXTERN OsStatus_t os_alloc_buffer(FILE *file);
__EXTERN OsStatus_t os_flush_buffer(FILE *file);
__EXTERN int os_flush_all_buffers(int mask);
__EXTERN OsStatus_t add_std_buffer(FILE *file);
__EXTERN void remove_std_buffer(FILE *file);

__EXTERN int _flsbuf(int ch, FILE *stream);
__EXTERN int _flswbuf(int ch, FILE *stream);

/* StdioFdAllocate 
 * Allocates a new file descriptor handle from the bitmap.
 * Returns -1 on error. */
__EXTERN int StdioFdAllocate(UUId_t handle, int flag);
__EXTERN OsStatus_t StdioFdInitialize(_In_ FILE *file, _In_ int fd, _In_ unsigned stream_flags);
__EXTERN void StdioFdFree(_In_ int fd);
__EXTERN UUId_t StdioFdToHandle(_In_ int fd);

/* StdioReadInternal
 * Internal read wrapper for file-reading */
__EXTERN
OsStatus_t
StdioReadInternal(
    _In_ int fd, 
    _In_ char *Buffer, 
    _In_ size_t Length,
    _Out_ size_t *BytesRead);

/* StdioSeekInternal
 * Internal wrapper for stdio's syntax, conversion to our own RPC syntax */
__EXTERN
OsStatus_t
StdioSeekInternal(
    _In_ int fd, 
    _In_ off64_t Offset, 
    _In_ int Origin);

#endif //!__STDIO_SUPPORT_H__
