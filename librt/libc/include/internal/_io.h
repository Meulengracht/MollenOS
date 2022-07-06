#ifndef __INTERNAL_IO_H__
#define __INTERNAL_IO_H__

#include <internal/_evt.h>
#include <internal/_ipc.h>
#include <internal/_ioset.h>
#include <internal/_pipe.h>
#include <internal/_socket.h>
#include <os/osdefs.h>
#include <os/spinlock.h>
#include <os/types/process.h>
#include <stdio.h>

#ifndef _IOCOMMIT
#define _IOCOMMIT 0x4000
#endif

// Values for wxflag
#define WX_OPEN             0x01U
#define WX_ATEOF            0x02U
#define WX_READNL           0x04U  // read started with \n
#define WX_READEOF          0x04U  // like ATEOF, but for underlying file rather than buffer
#define WX_PIPE             0x08U
#define WX_TEMP             0x10U  // delete underlying resource on close
#define WX_DONTINHERIT      0x20U
#define WX_APPEND           0x40U
#define WX_TTY              0x80U
#define WX_TEXT             0x100U
#define WX_WIDE             (WX_TEXT | 0x200U)
#define WX_UTF              (WX_TEXT | 0x400U)
#define WX_UTF16            (WX_WIDE | 0x800U)
#define WX_UTF32            (WX_WIDE | 0x1000U)
#define WX_BIGENDIAN        0x2000U
#define WX_TEXT_FLAGS       (WX_TEXT | WX_WIDE | WX_UTF | WX_UTF16 | WX_UTF32 | WX_BIGENDIAN)

#define WX_INHERITTED       0x00004000U
#define WX_PERSISTANT       0x00008000U
#define WX_PRIORITY         0x00010000U

#define INTERNAL_BUFSIZ     4096
#define INTERNAL_MAXFILES   1024

#define STDIO_HANDLE_INVALID    0
#define STDIO_HANDLE_PIPE       1
#define STDIO_HANDLE_FILE       2
#define STDIO_HANDLE_SOCKET     3
#define STDIO_HANDLE_IPCONTEXT  4
#define STDIO_HANDLE_SET        5
#define STDIO_HANDLE_EVENT      6

typedef struct stdio_handle stdio_handle_t;

// Inheritable handle that is shared with child processes
// should contain only portable information
typedef struct stdio_object {
    UUId_t handle;
    int    type;
    union {
        struct socket    socket;
        struct ipcontext ipcontext;
        struct pipe      pipe;
        struct ioset     ioset;
        struct evt       evt;
    } data;
} stdio_object_t;

#define STDIO_CLOSE_FULL    1
#define STDIO_CLOSE_DELETE  2

// Stdio descriptor operations
typedef oscode_t(*stdio_inherit)(stdio_handle_t*);
typedef oscode_t(*stdio_read)(stdio_handle_t*, void*, size_t, size_t*);
typedef oscode_t(*stdio_write)(stdio_handle_t*, const void*, size_t, size_t*);
typedef oscode_t(*stdio_resize)(stdio_handle_t*, long long);
typedef oscode_t(*stdio_seek)(stdio_handle_t*, int, off64_t, long long*);
typedef oscode_t(*stdio_ioctl)(stdio_handle_t*, int, va_list);
typedef oscode_t(*stdio_close)(stdio_handle_t*, int);

typedef struct stdio_ops {
    stdio_inherit inherit;
    stdio_read    read;
    stdio_write   write;
    stdio_resize  resize;
    stdio_seek    seek;
    stdio_ioctl   ioctl;
    stdio_close   close;
} stdio_ops_t;

// Local to application handle that also handles state, stream and buffer
// support for a handle.
typedef struct stdio_handle {
    int            fd;
    unsigned int   wxflag;
    spinlock_t     lock;
    stdio_object_t object;
    stdio_ops_t    ops;
    char           lookahead[3];
    FILE*          buffered_stream;
} stdio_handle_t;

typedef struct stdio_inheritation_block {
    int                 handle_count;
    struct stdio_handle handles[];
} stdio_inheritation_block_t;

// io-object interface
extern int             stdio_handle_create(int iod, int flags, stdio_handle_t**);
extern void            stdio_handle_clone(stdio_handle_t* target, stdio_handle_t* source);
extern int             stdio_handle_set_handle(stdio_handle_t*, UUId_t);
extern int             stdio_handle_set_ops_type(stdio_handle_t*, int);
extern int             stdio_handle_set_buffered(stdio_handle_t*, FILE*, unsigned int);
extern int             stdio_handle_destroy(stdio_handle_t*, int);
extern int             stdio_handle_activity(stdio_handle_t*, int);
extern void            stdio_handle_flag(stdio_handle_t*, unsigned int);
extern stdio_handle_t* stdio_handle_get(int iod);

// io-buffer interface
#define IO_IS_NOT_BUFFERED(stream) ((stream)->_flag & _IONBF)
#define IO_IS_BUFFERED(stream)     !IO_IS_NOT_BUFFERED(stream)
#define IO_HAS_BUFFER_DATA(stream) ((stream)->_cnt > 0)

extern void       io_buffer_ensure(FILE* stream);
extern void       io_buffer_allocate(FILE* stream);
extern oscode_t io_buffer_flush(FILE* file);
extern int        io_buffer_flush_all(int mask);

// io-operation types
extern void stdio_get_null_operations(stdio_ops_t* ops);
extern void stdio_get_pipe_operations(stdio_ops_t* ops);
extern void stdio_get_file_operations(stdio_ops_t* ops);
extern void stdio_get_net_operations(stdio_ops_t* ops);
extern void stdio_get_ipc_operations(stdio_ops_t* ops);
extern void stdio_get_set_operations(stdio_ops_t* ops);
extern void stdio_get_evt_operations(stdio_ops_t* ops);

// helpers
extern int          stdio_bitmap_initialize(void);
extern int          stdio_bitmap_allocate(int fd);
extern void         stdio_bitmap_free(int fd);
extern int          _flsbuf(int ch, FILE *stream);
extern int          _flswbuf(int ch, FILE *stream);
extern int          stream_ensure_mode(int mode, FILE* stream);
extern unsigned int _faccess(int oflags);
extern unsigned int _fopts(int oflags);
extern int          _fflags(const char *mode, int *open_flags, int *stream_flags);
extern oscode_t   _lock_stream(FILE * stream);
extern oscode_t   _unlock_stream(FILE * stream);
extern int          streamout(FILE *stream, const char *format, va_list argptr);
extern int          wstreamout(FILE *stream, const wchar_t *format, va_list argptr);


// Must be reentrancy spinlocks (critical sections)
#define LOCK_FILES() do { } while(0)
#define UNLOCK_FILES() do { } while(0)

extern oscode_t
StdioCreateInheritanceBlock(
	_In_  ProcessConfiguration_t* configuration,
    _Out_ void**                  inheritationBlockOut,
    _Out_ size_t*                 inheritationBlockLengthOut);

#endif //!__INTERNAL_IO_H__
