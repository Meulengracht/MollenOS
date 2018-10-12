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

#include <os/osdefs.h>
#include <os/spinlock.h>
#include <os/process.h>

#ifndef _IOCOMMIT
#define _IOCOMMIT 0x4000
#endif

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
#define WX_INHERITTED       0x100

/* values for exflag - it's used differently in msvcr90.dll*/
#define EF_UTF8             0x01
#define EF_UTF16            0x02
#define EF_UNK_UNICODE      0x08
#define EF_CLOSE            0x10

#define INTERNAL_BUFSIZ     4096
#define INTERNAL_MAXFILES   1024

#define STDIO_HANDLE_INVALID    0
#define STDIO_HANDLE_PIPE       1
#define STDIO_HANDLE_FILE       2

/* StdioHandle
 * Describes a handle that can be inherited by the process. 
 * These can only be created from existing handles (file, pipe etc) */
typedef struct _StdioHandle {
    int             InheritationType;
    union {
        struct {
            UUId_t  ProcessId;
            int     Port;
        } Pipe;
        UUId_t      FileHandle;
    } InheritationData;
} StdioHandle_t;

typedef struct {
    int                 fd;
    StdioHandle_t       handle;
    unsigned char       wxflag;
    char                lookahead[3];
    int                 exflag;
    void*               file;
    Spinlock_t          lock;
} StdioObject_t;

__EXTERN StdioObject_t* get_ioinfo(int fd);
__EXTERN OsStatus_t os_alloc_buffer(FILE *file);
__EXTERN OsStatus_t os_flush_buffer(FILE *file);
__EXTERN int os_flush_all_buffers(int mask);
__EXTERN OsStatus_t add_std_buffer(FILE *file);
__EXTERN void remove_std_buffer(FILE *file);

__EXTERN int _flsbuf(int ch, FILE *stream);
__EXTERN int _flswbuf(int ch, FILE *stream);

/* Stdio internal file-descriptor management functions 
 * Used internally by the c library to manage open file handles */
__EXTERN int StdioFdAllocate(int fd, int flag);
__EXTERN void StdioCreateFileHandle(UUId_t FileHandle, StdioObject_t *Object);
__EXTERN OsStatus_t StdioCreatePipeHandle(UUId_t ProcessId, int Port, int Oflags, StdioObject_t *Object);
__EXTERN OsStatus_t StdioFdInitialize(_In_ FILE *file, _In_ int fd, _In_ unsigned stream_flags);
__EXTERN void StdioFdFree(_In_ int fd);
__EXTERN StdioHandle_t* StdioFdToHandle(_In_ int fd);

/* StdioCreateInheritanceBlock
 * Creates a block of data containing all the stdio handles that
 * can be inherited. */
__EXTERN OsStatus_t
StdioCreateInheritanceBlock(
	_In_ ProcessStartupInformation_t* StartupInformation);

/* StdioReadInternal
 * Internal read wrapper for file-reading */
__EXTERN
OsStatus_t
StdioReadInternal(
    _In_ int fd, 
    _In_ char *Buffer, 
    _In_ size_t Length,
    _Out_ size_t *BytesRead);

/* StdioWriteInternal
 * Internal write wrapper for file-writing */
__EXTERN
OsStatus_t
StdioWriteInternal(
    _In_ int fd, 
    _Out_ char *Buffer, 
    _In_ size_t Length,
    _Out_ size_t *BytesWritten);

/* StdioSeekInternal
 * Internal wrapper for stdio's syntax, conversion to our own RPC syntax */
__EXTERN
OsStatus_t
StdioSeekInternal(
    _In_ int fd, 
    _In_ off64_t Offset, 
    _In_ int Origin,
    _Out_ long long *Position);

#endif //!__STDIO_SUPPORT_H__
