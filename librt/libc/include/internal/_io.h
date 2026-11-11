#ifndef __INTERNAL_IO_H__
#define __INTERNAL_IO_H__

#include <os/types/handle.h>
#include <os/types/process.h>
#include <stdio.h>

// Values for XTFlags
#define __IO_READ       0x1
#define __IO_WRITE      0x2
#define __IO_RW         (__IO_READ | __IO_WRITE)
#define __IO_APPEND     0x4U
#define __IO_NOINHERIT  0x8U

#define __IO_TEXTMODE  0x10U
#define __IO_WIDE      (__IO_TEXTMODE | 0x20U)
#define __IO_UTF       (__IO_TEXTMODE | 0x40U)
#define __IO_UTF16     (__IO_WIDE | 0x80U)
#define __IO_UTF32     (__IO_WIDE | 0x100U)
#define __IO_BIGENDIAN 0x200U
#define __IO_TEXTMASK  (__IO_TEXTMODE | __IO_WIDE | __IO_UTF | __IO_UTF16 | __IO_UTF32 | __IO_BIGENDIAN)

#define __IO_READNL         0x00001000U  // read started with \n
#define __IO_PIPE           0x00002000U
#define __IO_TTY            0x00004000U
#define __IO_OPEN           0x00001000U
#define __IO_NONBLOCK       0x00002000U
#define __IO_PERSISTANT     0x00004000U
#define __IO_PRIORITY       0x00008000U

// File mode bits permitted in an inheritance descriptor.
#define __IO_INHERIT_FLAGS (__IO_RW | __IO_APPEND | __IO_NOINHERIT | __IO_TEXTMASK)

#define INTERNAL_BUFSIZ     4096
#define INTERNAL_MAXFILES   1024

typedef struct StdioDescriptor stdio_handle_t;

#define STDIO_CLOSE_FULL    1
#define STDIO_CLOSE_DELETE  2

/**
 * @brief Operations for one descriptor signature. Process inheritance does not
 * use these operations. It uses the type table in stdio/core/inheritance.c,
 * and only accepts descriptors which have Shared set.
 */
typedef struct StdioOperations {
    oserr_t (*read)(stdio_handle_t*, void*, size_t, size_t*);
    oserr_t (*write)(stdio_handle_t*, const void*, size_t, size_t*);
    oserr_t (*resize)(stdio_handle_t*, long long);
    oserr_t (*seek)(stdio_handle_t*, int, off64_t, long long*);
    oserr_t (*ioctl)(stdio_handle_t*, int, va_list);
    void    (*close)(stdio_handle_t*, int);
} stdio_ops_t;

/**
 * @brief Local to application handle that also handles state, stream and buffer
 * support for a handle.
 *
 * Shared is set by the code that creates the descriptor. When set, copies of
 * the descriptor share one registered OS handle. Each copy holds a local handle
 * reference, and close releases that reference instead of closing the operation
 * context. Copies of descriptors without Shared are marked __IO_PERSISTANT.
 */
typedef struct StdioDescriptor {
    int          IOD;
    unsigned int Signature;
    unsigned int XTFlags;
    bool         Shared;
    OSHandle_t   OSHandle;
    stdio_ops_t* Ops;
    void*        OpsContext;
    char         Peek[3];
    FILE*        Stream;
} stdio_handle_t;

struct stdio_object_entry {
    int             id;
    stdio_handle_t* handle;
};

#define NULL_SIGNATURE         0x80000000
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
 * @param xtFlags The WX_* flags the handle should be created with.
 * @param ops     The underlying stdio operations.
 * @param opsCtx  The context that should be passed to operations.
 * @param handleOut Where the resulting handle should be stored.
 * @return
 */
extern int
stdio_handle_create(
        _In_  int              iod,
        _In_  int              ioFlags,
        _In_  unsigned int     xtFlags,
        _In_  unsigned int     signature,
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
 * @brief Creates a new descriptor for the same OS handle. The operation context
 * is not copied, so the new descriptor starts with a NULL context.
 * @param handle The descriptor to duplicate.
 * @param handleOut Receives the new descriptor.
 * @return 0 on success, otherwise -1 with errno set.
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
 * @brief Marks the stdio handle as shared, indicating that
 * it can be safely duplicated and used across multiple references.
 */
extern void stdio_handle_set_shared(
    _In_ stdio_handle_t* handle);

/**
 * @brief
 * @param handle
 * @param stream
 * @param flags
 * @return
 */
extern int
stdio_handle_set_buffered(
        _In_ stdio_handle_t* handle,
        _In_ FILE*           stream,
        _In_ uint16_t        flags,
        _In_ uint8_t         bufferMode);

/**
 * @brief
 * @param handle
 * @param activity
 * @return
 */
extern int
stdio_handle_activity(
        _In_ stdio_handle_t* handle,
        _In_ int             activity);

/**
 * @brief
 * @param handle
 * @param xtFlags
 */
extern void
stdio_handle_set_flags(
        _In_ stdio_handle_t* handle,
        _In_ unsigned int    xtFlags);

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
 * @brief Retrieves the IOD of the stdio handle.
 * @param handle The stdio handle.
 * @return The IOD of the handle.
 */
extern int
stdio_handle_iod(
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
extern void    io_buffer_ensure(FILE* stream);
extern void    io_buffer_allocate(FILE* stream);
extern oserr_t io_buffer_flush(FILE* file);
extern void    io_buffer_flush_all(uint16_t flags);

// helpers
extern int  stdio_bitmap_initialize(void);
extern int  stdio_bitmap_allocate(int fd);
extern void stdio_bitmap_free(int fd);

extern unsigned int _faccess(int oflags);
extern unsigned int _fperms(unsigned int mode);
extern unsigned int _fopts(int oflags);
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

/**
 * @brief Temporary export controls for one spawn operation. The serialized block
 * carries tokens, while this local list keeps the controls available for cleanup.
 */
struct inheritance_export {
    uuid_t*  controls;
    uint32_t count;
    uint32_t length;
};

/**
 * @brief Serializes selected files, pipes and sockets. Keep the descriptor table
 * unchanged during this call. Release the export after the spawn RPC finishes.
 *
 * @param options Descriptor selection and standard descriptor remapping.
 * @param buffer Destination for the bounded contract payload.
 * @param capacity Available bytes in buffer.
 * @param exportOut Receives controls and encoded length. Empty on failure.
 * @return OS_EOK, or a selection, capacity, allocation or token error.
 */
extern oserr_t
CRTWriteInheritanceBlock(
    _In_  const struct InheritanceOptions* options,
    _Out_ void*                            buffer,
    _In_  uint32_t                         capacity,
    _Out_ struct inheritance_export*       exportOut);

/**
 * @brief Revokes temporary export controls and clears the local export state.
 * @param exported State returned by CRTWriteInheritanceBlock, or zeroed state.
 */
extern void
CRTReleaseInheritanceBlock(
    _InOut_ struct inheritance_export* exported);

/**
 * @brief Adopts installed child references and constructs selected descriptors.
 * Must run once before application constructors. On failure the caller exits;
 * process cleanup releases any installed grants not registered locally.
 *
 * @param buffer Private startup output section. May be NULL when empty.
 * @param length Exact byte length of the section.
 * @return OS_EOK, or a payload, occupied descriptor or allocation error.
 */
extern oserr_t
CRTReadInheritanceBlock(
    _In_ const void* buffer,
    _In_ uint32_t    length);

#endif //!__INTERNAL_IO_H__
