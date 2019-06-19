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
 * Standard C Support
 * - Standard IO Support header
 */

#ifndef __STDIO_IO_H__
#define __STDIO_IO_H__

#include <os/osdefs.h>
#include <ds/collection.h>
#include <os/spinlock.h>
#include <os/types/process.h>
#include <stdio.h>

#ifndef _IOCOMMIT
#define _IOCOMMIT 0x4000
#endif

// Values for wxflag
#define WX_OPEN             0x01
#define WX_ATEOF            0x02
#define WX_READNL           0x04  // read started with \n
#define WX_READEOF          0x04  // like ATEOF, but for underlying file rather than buffer
#define WX_PIPE             0x08
#define WX_READCR           0x08  // underlying file is at \r
#define WX_DONTINHERIT      0x10
#define WX_APPEND           0x20
#define WX_TTY              0x40
#define WX_TEXT             0x80
#define WX_WIDE             0x100
#define WX_UTF              (WX_TEXT | 0x200)
#define WX_INHERITTED       0x400
#define WX_PERSISTANT       0x800

#define INTERNAL_BUFSIZ     4096
#define INTERNAL_MAXFILES   1024

#define STDIO_HANDLE_INVALID    0
#define STDIO_HANDLE_PIPE       1
#define STDIO_HANDLE_FILE       2
#define STDIO_HANDLE_SOCKET     3

// Inheritable handle that is shared with child processes
// should contain only portable information
typedef struct {
    int    InheritationType;
    UUId_t InheritationHandle;
} stdio_handle_t;

// Stdio descriptor operations
typedef OsStatus_t(*stdio_read)(stdio_handle_t*, const void*, size_t, size_t*);
typedef OsStatus_t(*stdio_write)(stdio_handle_t*, void*, size_t, size_t*);
typedef OsStatus_t(*stdio_resize)(stdio_handle_t*, long long);
typedef OsStatus_t(*stdio_seek)(stdio_handle_t*, int, off64_t, long long*);
typedef OsStatus_t(*stdio_close)(stdio_handle_t*, int);

typedef struct {
    stdio_read   read;
    stdio_write  write;
    stdio_resize resize;
    stdio_seek   seek;
    stdio_close  close;
} stdio_ops_t;

// Local to application handle that also handles state, stream and buffer
// support for a handle.
typedef struct {
    int            fd;
    spinlock_t     lock;
    stdio_handle_t handle;
    stdio_ops_t    ops;
    unsigned short wxflag;
    char           lookahead[3];
    FILE*          buffered_stream;
} stdio_object_t;

// io-object interface
extern int             stdio_object_create(int, int, stdio_object_t**);
extern int             stdio_object_set_handle(stdio_object_t*, UUId_t);
extern int             stdio_object_set_ops_type(stdio_object_t*, int);
extern int             stdio_object_set_buffered(stdio_object_t*, FILE*, unsigned int);
extern int             stdio_object_destroy(stdio_object_t*, int);
extern stdio_object_t* stdio_object_get(int fd);
extern Collection_t*   stdio_get_objects(void);

// io-buffer interface
extern OsStatus_t os_alloc_buffer(FILE* file);
extern OsStatus_t os_flush_buffer(FILE* file);
extern int        os_flush_all_buffers(int mask);
extern OsStatus_t add_std_buffer(FILE* file);
extern void       remove_std_buffer(FILE* file);

// io-operation types
extern void stdio_get_null_operations(stdio_ops_t* ops);
extern void stdio_get_pipe_operations(stdio_ops_t* ops);
extern void stdio_get_file_operations(stdio_ops_t* ops);
extern void stdio_get_net_operations(stdio_ops_t* ops);

// helpers
extern int  stdio_bitmap_initialize(void);
extern int  stdio_bitmap_allocate(int fd);
extern void stdio_bitmap_free(int fd);
extern int  _flsbuf(int ch, FILE *stream);
extern int  _flswbuf(int ch, FILE *stream);

// Must be reentrancy spinlocks (critical sections)
#define LOCK_FILES() do { } while(0)
#define UNLOCK_FILES() do { } while(0)

extern OsStatus_t
StdioCreateInheritanceBlock(
	_In_  ProcessStartupInformation_t* StartupInformation,
    _Out_ void**                       InheritationBlock,
    _Out_ size_t*                      InheritationBlockLength);

#endif //!__STDIO_IO_H__
