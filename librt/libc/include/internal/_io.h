#ifndef __INTERNAL_IO_H__
#define __INTERNAL_IO_H__

#include <internal/_evt.h>
#include <internal/_ipc.h>
#include <internal/_ioset.h>
#include <internal/_pipe.h>
#include <internal/_socket.h>
#include <os/osdefs.h>
#include <os/types/handle.h>
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

typedef struct stdio_handle stdio_handle_t;

#define STDIO_CLOSE_FULL    1
#define STDIO_CLOSE_DELETE  2

// Stdio descriptor operations
typedef oserr_t(*stdio_serialize)(stdio_handle_t*, void*);
typedef oserr_t(*stdio_deserialize)(stdio_handle_t*, const void*);
typedef oserr_t(*stdio_read)(stdio_handle_t*, void*, size_t, size_t*);
typedef oserr_t(*stdio_write)(stdio_handle_t*, const void*, size_t, size_t*);
typedef oserr_t(*stdio_resize)(stdio_handle_t*, long long);
typedef oserr_t(*stdio_seek)(stdio_handle_t*, int, off64_t, long long*);
typedef oserr_t(*stdio_ioctl)(stdio_handle_t*, int, va_list);
typedef oserr_t(*stdio_mmap)(stdio_handle_t*, void *addr, size_t length, int prot, int flags, off_t offset, void**);
typedef oserr_t(*stdio_munmap)(stdio_handle_t*, void *addr, size_t length);
typedef void   (*stdio_close)(stdio_handle_t*, int);

typedef struct stdio_ops {
    stdio_serialize   serialize;
    stdio_deserialize deserialize;
    stdio_read        read;
    stdio_write       write;
    stdio_resize      resize;
    stdio_seek        seek;
    stdio_ioctl       ioctl;
    stdio_mmap        mmap;
    stdio_munmap      munmap;
    stdio_close       close;
} stdio_ops_t;

// Local to application handle that also handles state, stream and buffer
// support for a handle.
typedef struct stdio_handle {
    int            fd;
    unsigned int   wxflag;
    OSHandle_t     handle;
    stdio_ops_t    ops;
    void*          ops_ctx;
    char           lookahead[3];
    FILE*          buffered_stream;
} stdio_handle_t;

typedef struct stdio_inheritation_block {
    int                 handle_count;
    struct stdio_handle handles[];
} stdio_inheritation_block_t;

struct stdio_object_entry {
    int             id;
    stdio_handle_t* handle;
};

// io-object interface
extern int  stdio_handle_create(int iod, int flags, stdio_handle_t**);

extern int  stdio_handle_set_ops(stdio_handle_t*, stdio_ops_t*);
extern int  stdio_handle_set_ops_ctx(stdio_handle_t*, void*);
extern int  stdio_handle_set_buffered(stdio_handle_t*, FILE*, unsigned int);
extern void stdio_handle_destroy(stdio_handle_t*);
extern int  stdio_handle_activity(stdio_handle_t*, int);
extern void stdio_handle_flag(stdio_handle_t*, unsigned int);

#define FMEM_SIGNATURE         0x80000001
#define MEMORYSTREAM_SIGNATURE 0x80000002
#define PIPE_SIGNATURE         0x80000003
#define FILE_SIGNATURE         0x80000004
#define IPC_SIGNATURE          0x80000005
#define EVENT_SIGNATURE        0x80000006
#define IOSET_SIGNATURE        0x80000007
#define NET_SIGNATURE          0x80000008

/**
 * @brief Convert from file mode ASCII string to O_* flags.
 * @param mode An ASCII string containing valid file mode string
 * @param flagsOut O_* style flags
 * @return -1 if the string contained illegal characters, otherwise 0.
 */
extern int __fmode_to_flags(const char* mode, int* flagsOut);

/**
 * @brief Creates a new stdio resource handle
 * @param iod     If not -1, then attempts to pre-assign it this io-descriptor.
 * @param ioFlags The O_* flags the handle should be configured with.
 * @param wxFlags The WX_* flags the handle should be created with.
 * @param ops     The underlying stdio operations.
 * @param opsCtx  The context that should be passed to operations.
 * @param handleOut Where the resulting handle should be stored.
 * @return
 */
extern int
stdio_handle_create2(
        _In_  int              iod,
        _In_  int              ioFlags,
        _In_  int              wxFlags,
        _In_  unsigned int     signature,
        _In_  stdio_ops_t*     ops,
        _In_  void*            opsCtx,
        _Out_ stdio_handle_t** handleOut);

/**
 * @brief Destroys any resources associated with the stdio handle object.
 * @param handle The stdio handle to delete.
 */
extern void
stdio_handle_delete(
        _In_ stdio_handle_t* handle);

/**
 * @brief
 * @param handle
 * @param handleOut
 * @return
 */
extern int
stdio_handle_clone(
        _In_  stdio_handle_t*  handle,
        _Out_ stdio_handle_t** handleOut);

/**
 * @brief Attaches an OSHandle with the io handle.
 * @param handle
 * @param osHandle
 * @return
 */
extern int
stdio_handle_set_handle(
        _In_ stdio_handle_t* handle,
        _In_ OSHandle_t*     osHandle);

/**
 * @brief Retrieves the signature for the stdio handle. This can be used
 * to differentiate the types of handles by operation functions.
 * @param handle
 * @return
 */
extern unsigned int
stdio_handle_signature(
        _In_ stdio_handle_t* handle);

/**
 * @brief Returns the FILE stream associated with this stdio handle.
 * @param handle The stdio handle to retrieve the asssociated FILE stream from.
 * @return The retrieved FILE stream. NULL if the handle is not buffered.
 */
extern FILE*
stdio_handle_stream(
        _In_ stdio_handle_t* handle);

/**
 * Retrievs the stdio handle object from the io-descriptor.
 * @param iod The io-descriptor to lookup.
 * @return The stdio_handle_t object.
 */
extern stdio_handle_t*
stdio_handle_get(
        _In_ int iod);

// io-buffer interface
#define IO_IS_NOT_BUFFERED(stream) ((stream)->_flag & _IONBF)
#define IO_IS_BUFFERED(stream)     !IO_IS_NOT_BUFFERED(stream)
#define IO_HAS_BUFFER_DATA(stream) ((stream)->_cnt > 0)

extern void    io_buffer_ensure(FILE* stream);
extern void    io_buffer_allocate(FILE* stream);
extern oserr_t io_buffer_flush(FILE* file);
extern int     io_buffer_flush_all(int mask);

// io-operation types
extern void stdio_get_null_operations(stdio_ops_t* ops);
extern void stdio_get_pipe_operations(stdio_ops_t* ops);
extern void stdio_get_file_operations(stdio_ops_t* ops);
extern void stdio_get_net_operations(stdio_ops_t* ops);
extern void stdio_get_ipc_operations(stdio_ops_t* ops);
extern void stdio_get_set_operations(stdio_ops_t* ops);
extern void stdio_get_evt_operations(stdio_ops_t* ops);

// helpers
extern int  stdio_bitmap_initialize(void);
extern int  stdio_bitmap_allocate(int fd);
extern void stdio_bitmap_free(int fd);

/**
 * @brief _flsbuf/_flswbuf does not lock the stream it is given. It is expected that the stream must be
 * locked when calling this method.
 * @param ch
 * @param stream
 * @return
 */
extern int _flsbuf(int ch, FILE *stream);
extern int _flswbuf(int ch, FILE *stream);

extern int          stream_ensure_mode(int mode, FILE* stream);
extern unsigned int _faccess(int oflags);
extern unsigned int _fperms(unsigned int mode);
extern unsigned int _fopts(int oflags);
extern int          _fflags(const char *mode, int *open_flags, int *stream_flags);
extern int          streamout(FILE *stream, const char *format, va_list argptr);
extern int          wstreamout(FILE *stream, const wchar_t *format, va_list argptr);


// Must be reentrancy spinlocks (critical sections)
// TODO: implement this
#define LOCK_FILES() do { } while(0)
#define UNLOCK_FILES() do { } while(0)

struct InheritanceOptions {
    unsigned int Flags;
    int          StdOutHandle;
    int          StdInHandle;
    int          StdErrHandle;
};

extern void
CRTWriteInheritanceBlock(
	_In_  struct InheritanceOptions* options,
    _In_  void*                      buffer,
    _Out_ uint32_t*                  lengthWrittenOut);

#endif //!__INTERNAL_IO_H__
