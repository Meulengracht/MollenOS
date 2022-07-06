/**
 * Stdio definitions and includes for unit test environment
 */

struct _iobuf {
    int _fd;
    char* _ptr;
    int _cnt;
    char* _base;
    int _flag;
    int _charbuf;
    int _bufsiz;
    char* _tmpfname;
};
typedef struct _iobuf FILE;
typedef struct stdio_handle stdio_handle_t;

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

typedef struct stdio_object {
    UUId_t handle;
    int    type;
} stdio_object_t;

// Local to application handle that also handles state, stream and buffer
// support for a handle.
typedef struct stdio_handle {
    int            fd;
    //spinlock_t     lock;
    stdio_object_t object;
    stdio_ops_t    ops;
    unsigned short wxflag;
    char           lookahead[3];
    FILE*          buffered_stream;
} stdio_handle_t;

// Values for wxflag
#define WX_OPEN             0x01U
#define WX_ATEOF            0x02U
#define WX_READNL           0x04U  // read started with \n
#define WX_READEOF          0x04U  // like ATEOF, but for underlying file rather than buffer
#define WX_PIPE             0x08U
#define WX_READCR           0x10U  // underlying file is at \r
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

#define _IOFBF     0x0000
#define _IOREAD	   0x0001
#define _IOWRT     0x0002
#define _IONBF     0x0004
#define _IOMYBUF   0x0008
#define _IOEOF     0x0010
#define _IOERR     0x0020
#define _IOLBF     0x0040
#define _IOSTRG    0x0040
#define _IORW      0x0080
#define _USERBUF   0x0100
#define _FWIDE     0x0200
#define _FBYTE     0x0400
#define _IOVRT     0x0800

#define STDIO_HANDLE_INVALID    0
#define STDIO_HANDLE_PIPE       1
#define STDIO_HANDLE_FILE       2
#define STDIO_HANDLE_SOCKET     3
#define STDIO_HANDLE_IPCONTEXT  4
#define STDIO_HANDLE_SET        5
#define STDIO_HANDLE_EVENT      6

#define EOF				(-1)
#define SEEK_SET        0 /* Seek from beginning of file.  */
#define SEEK_CUR        1 /* Seek from current position.  */
#define SEEK_END        2 /* Set file pointer to EOF plus "offset" */
#define BUFSIZ          (int)2048
