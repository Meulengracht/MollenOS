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

#include <internal/_file.h>

#ifdef LIBC_KERNEL
#include <assert.h>
#include <spinlock.h>
#include <threading.h>
#include <stdio.h>

static Spinlock_t g_printLock = OS_SPINLOCK_INIT;
static FILE g_stdout = { 0 };
static FILE g_stdin  = { 0 };
static FILE g_stderr = { 0 };

void usched_mtx_init(struct usched_mtx* mutex, int type) {
    _CRT_UNUSED(mutex);
    _CRT_UNUSED(type);
}

void flockfile(FILE* stream) {
    assert(stream != NULL);
    SpinlockAcquire(&g_printLock);
}

void funlockfile(FILE* stream) {
    assert(stream != NULL);
    SpinlockRelease(&g_printLock);
}

FILE*
__get_std_handle(
    _In_ int n)
{
    switch (n) {
        case STDOUT_FILENO: {
            return &g_stdout;
        }
        case STDIN_FILENO: {
            return &g_stdin;
        }
        case STDERR_FILENO: {
            return &g_stderr;
        }
        default: {
            return NULL;
        }
    }
}

int wctomb(char *mbchar, wchar_t wchar) {
    _CRT_UNUSED(mbchar);
    _CRT_UNUSED(wchar);
    return 0;
}

uuid_t ThreadsCurrentId(void) {
    return ThreadCurrentHandle();
}

#else
//#define __TRACE
#include <assert.h>
#include <ddk/handle.h>
#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <errno.h>
#include <internal/_io.h>
#include <io.h>

// hashtable functions for g_stdioObjects
static uint64_t stdio_hash(const void* element);
static int      stdio_cmp(const void* element1, const void* element2);

static hashtable_t g_stdioObjects = { 0 };
static FILE        g_stdout       = { 0 };
static FILE        g_stdint       = { 0 };
static FILE        g_stderr       = { 0 };

/**
 * Returns whether or not the handle should be inheritted by sub-processes based on the requested
 * startup information and the handle settings.
 */
static oserr_t
StdioIsHandleInheritable(
    _In_ ProcessConfiguration_t* configuration,
    _In_ stdio_handle_t*         handle)
{
    oserr_t osSuccess = OS_EOK;

    if (handle->wxflag & WX_DONTINHERIT) {
        osSuccess = OS_EUNKNOWN;
    }

    // If we didn't request to inherit one of the handles, then we don't account it
    // for being the one requested.
    if (handle->fd == configuration->StdOutHandle &&
        !(configuration->InheritFlags & PROCESS_INHERIT_STDOUT)) {
        osSuccess = OS_EUNKNOWN;
    } else if (handle->fd == configuration->StdInHandle &&
             !(configuration->InheritFlags & PROCESS_INHERIT_STDIN)) {
        osSuccess = OS_EUNKNOWN;
    } else if (handle->fd == configuration->StdErrHandle &&
             !(configuration->InheritFlags & PROCESS_INHERIT_STDERR)) {
        osSuccess = OS_EUNKNOWN;
    } else if (!(configuration->InheritFlags & PROCESS_INHERIT_FILES)) {
        if (handle->fd != configuration->StdOutHandle &&
            handle->fd != configuration->StdInHandle &&
            handle->fd != configuration->StdErrHandle) {
            osSuccess = OS_EUNKNOWN;
        }
    }

    TRACE("[can_inherit] iod %i, handle %u: %s",
            handle->fd, handle->object.handle,
          (osSuccess == OS_EOK) ? "yes" : "no");
    return osSuccess;
}

struct __get_inherit_context {
    ProcessConfiguration_t* configuration;
    int                     file_count;
};

static void __count_inherit_entry(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct stdio_object_entry* entry  = element;
    stdio_handle_t*                  object = entry->handle;
    struct __get_inherit_context*    context = userContext;
    _CRT_UNUSED(index);

    if (StdioIsHandleInheritable(context->configuration, object) == OS_EOK) {
        context->file_count++;
    }
}

static int
StdioGetNumberOfInheritableHandles(
    _In_ ProcessConfiguration_t* configuration)
{
    struct __get_inherit_context context = {
            .configuration = configuration,
            .file_count = 0
    };
    LOCK_FILES();
    hashtable_enumerate(
            &g_stdioObjects,
            __count_inherit_entry,
            &context
    );
    UNLOCK_FILES();
    return context.file_count;
}

struct __create_inherit_context {
    ProcessConfiguration_t*     configuration;
    stdio_inheritation_block_t* inheritation_block;
    int                         i;
};

static void __create_inherit_entry(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct stdio_object_entry* entry  = element;
    stdio_handle_t*                  object = entry->handle;
    struct __create_inherit_context* context = userContext;
    _CRT_UNUSED(index);

    if (StdioIsHandleInheritable(context->configuration, object) == OS_EOK) {
        memcpy(
                &context->inheritation_block->handles[context->i],
                object,
                sizeof(struct stdio_handle)
        );

        // Check for this fd to be equal to one of the custom handles
        // if it is equal, we need to update the fd of the handle to our reserved
        if (object->fd == context->configuration->StdOutHandle) {
            context->inheritation_block->handles[context->i].fd = STDOUT_FILENO;
        }
        if (object->fd == context->configuration->StdInHandle) {
            context->inheritation_block->handles[context->i].fd = STDIN_FILENO;
        }
        if (object->fd == context->configuration->StdErrHandle) {
            context->inheritation_block->handles[context->i].fd = STDERR_FILENO;
        }
        context->i++;
    }
}

oserr_t
StdioCreateInheritanceBlock(
    _In_  ProcessConfiguration_t* configuration,
    _Out_ void**                  inheritationBlockOut,
    _Out_ size_t*                 inheritationBlockLengthOut)
{
    struct __create_inherit_context context;
    stdio_inheritation_block_t*     inheritationBlock;
    int                             numberOfObjects;

    assert(configuration != NULL);

    if (configuration->InheritFlags == PROCESS_INHERIT_NONE) {
        return OS_EOK;
    }

    numberOfObjects = StdioGetNumberOfInheritableHandles(configuration);
    if (numberOfObjects != 0) {
        size_t inheritationBlockLength;

        inheritationBlockLength = sizeof(stdio_inheritation_block_t) + (numberOfObjects * sizeof(struct stdio_handle));
        inheritationBlock       = (stdio_inheritation_block_t*)malloc(inheritationBlockLength);
        if (!inheritationBlock) {
            return OS_EOOM;
        }

        TRACE("[add_inherit] length %u", inheritationBlockLength);
        inheritationBlock->handle_count = numberOfObjects;

        context.configuration = configuration;
        context.inheritation_block = inheritationBlock;
        context.i = 0;
        
        LOCK_FILES();
        hashtable_enumerate(
                &g_stdioObjects,
                __create_inherit_entry,
                &context
        );
        UNLOCK_FILES();
        
        *inheritationBlockOut       = (void*)inheritationBlock;
        *inheritationBlockLengthOut = inheritationBlockLength;
    }
    return OS_EOK;
}

static void
StdioInheritObject(
    _In_ struct stdio_handle* inheritHandle)
{
    stdio_handle_t* handle;
    int             status;
    TRACE("[inhert] iod %i, handle %u", inheritHandle->fd, inheritHandle->object.handle);
    
    status = stdio_handle_create(inheritHandle->fd, inheritHandle->wxflag | WX_INHERITTED, &handle);
    if (!status) {
        if (handle->fd == STDOUT_FILENO) {
            g_stdout._fd = handle->fd;
        }
        else if (handle->fd == STDIN_FILENO) {
            g_stdint._fd = handle->fd;
        }
        else if (handle->fd == STDERR_FILENO) {
            g_stderr._fd = handle->fd;
        }

        stdio_handle_clone(handle, inheritHandle);
        if (handle->ops.inherit(handle) != OS_EOK) {
            TRACE(" > failed to inherit fd %i", inheritHandle->fd);
            stdio_handle_destroy(handle, 0);
        }
    }
    else {
        WARNING(" > failed to create inheritted handle with fd %i", inheritHandle->fd);
    }
}

void StdioConfigureStandardHandles(
    _In_ void* inheritanceBlock)
{
    stdio_inheritation_block_t* block = inheritanceBlock;
    stdio_handle_t*             handle_out;
    stdio_handle_t*             handle_in;
    stdio_handle_t*             handle_err;
    int                         i;
    TRACE("[libc] [parse_inherit] 0x%" PRIxIN, block);
    
    // Handle inheritance
    if (block != NULL) {
        for (i = 0; i < block->handle_count; i++) {
            StdioInheritObject(&block->handles[i]);
        }
    }

    // Make sure all default handles have been set for std. The operations for
    // stdout and stderr will be null operations, as no output has been specified
    // for this process. If the process wants to get output it must reopen the
    // stdout/stderr handles.
    handle_out = stdio_handle_get(STDOUT_FILENO);
    if (!handle_out) {
        stdio_handle_create(STDOUT_FILENO, WX_DONTINHERIT, &handle_out);
    }

    handle_in = stdio_handle_get(STDIN_FILENO);
    if (!handle_in) {
        stdio_handle_create(STDIN_FILENO, WX_DONTINHERIT, &handle_in);
    }
    
    handle_err = stdio_handle_get(STDERR_FILENO);
    if (!handle_err) {
        stdio_handle_create(STDERR_FILENO, WX_DONTINHERIT, &handle_err);
    }

    // pre-initialize some of the data members which are not reset by
    // stdio_handle_set_buffered.
    usched_mtx_init(&g_stdout._lock, USCHED_MUTEX_RECURSIVE);
    usched_mtx_init(&g_stdint._lock, USCHED_MUTEX_RECURSIVE);
    usched_mtx_init(&g_stderr._lock, USCHED_MUTEX_RECURSIVE);

    stdio_handle_set_buffered(handle_out, &g_stdout, _IOWRT | _IOLBF); // we buffer stdout as default
    stdio_handle_set_buffered(handle_in, &g_stdint, _IOREAD | _IOLBF); // we also buffer stdint as default
    stdio_handle_set_buffered(handle_err, &g_stderr, _IOWRT | _IONBF);
}

struct __iod_close_context {
    unsigned int excludeFlags;
    int          filesClosed;
};

static void __close_entry(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    struct stdio_object_entry*  entry = (struct stdio_object_entry*)element;
    stdio_handle_t*             handle = entry->handle;
    struct __iod_close_context* context = userContext;
    _CRT_UNUSED(index);

    if (handle == NULL) {
        // already cleaned up
        return;
    }

    if (context->excludeFlags && (handle->wxflag & context->excludeFlags)) {
        return;
    }

    // Is it a buffered stream or raw?
    if (handle->buffered_stream) {
        fclose(handle->buffered_stream);
    } else {
        close(handle->fd);
    }

    // Cleanup handle, and mark the handle NULL to not cleanup twice.
    // We do a *VERY* crude cleanup here as we are called on shutdown
    // which means we don't really care about proper cleanup
    entry->handle = NULL;

    context->filesClosed++;
}

static int __close_io_descriptors(unsigned int excludeFlags)
{
    struct __iod_close_context context = {
            .excludeFlags = excludeFlags,
            .filesClosed  = 0
    };
    
    LOCK_FILES();
    hashtable_enumerate(
            &g_stdioObjects,
            __close_entry,
            &context
    );
    UNLOCK_FILES();
    return context.filesClosed;
}

int stdio_handle_create(int fd, int flags, stdio_handle_t** handle_out)
{
    stdio_handle_t* handle;
    int             updated_fd;

    // the bitmap allocator handles both cases if we want to allocate a specific
    // or just the first free fd
    updated_fd = stdio_bitmap_allocate(fd);
    if (updated_fd == -1) {
        _set_errno(EMFILE);
        return -1;
    }

    handle = (stdio_handle_t*)malloc(sizeof(stdio_handle_t));
    if (!handle) {
        _set_errno(ENOMEM);
        return -1;
    }
    memset(handle, 0, sizeof(stdio_handle_t));
    
    handle->fd            = updated_fd;
    handle->object.handle = UUID_INVALID;
    handle->object.type   = STDIO_HANDLE_INVALID;
    
    handle->wxflag       = WX_OPEN | flags;
    handle->lookahead[0] = '\n';
    handle->lookahead[1] = '\n';
    handle->lookahead[2] = '\n';
    stdio_get_null_operations(&handle->ops);

    hashtable_set(&g_stdioObjects, &(struct stdio_object_entry) {
        .id = updated_fd,
        .handle = handle
    });
    TRACE("[stdio_handle_create] success %i", updated_fd);
    
    *handle_out = handle;
    return EOK;
}

void stdio_handle_clone(stdio_handle_t* target, stdio_handle_t* source)
{
    if (!target || !source) {
        return;
    }

    // Copy the stdio object data, and then update ops
    memcpy(&target->object, &source->object, sizeof(stdio_object_t));
    stdio_handle_set_ops_type(target, source->object.type);
}

int stdio_handle_set_handle(stdio_handle_t* handle, uuid_t io_handle)
{
    if (!handle) {
        return EBADF;
    }
    handle->object.handle = io_handle;
    return EOK;
}

int stdio_handle_set_ops_type(stdio_handle_t* handle, int type)
{
    if (!handle) {
        return EBADF;
    }
    
    // Get io operations
    handle->object.type = type;
    switch (type) {
        case STDIO_HANDLE_PIPE: {
            stdio_get_pipe_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_FILE: {
            stdio_get_file_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_SOCKET: {
            stdio_get_net_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_IPCONTEXT: {
            stdio_get_ipc_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_SET: {
            stdio_get_set_operations(&handle->ops);
        } break;
        case STDIO_HANDLE_EVENT: {
            stdio_get_evt_operations(&handle->ops);
        } break;
        
        default: {
            stdio_get_null_operations(&handle->ops);
        } break;
    }
    return EOK;
}

int stdio_handle_set_buffered(stdio_handle_t* handle, FILE* stream, unsigned int stream_flags)
{
    if (!handle) {
        return EBADF;
    }
    
    if (!stream) {
        stream = (FILE*)malloc(sizeof(FILE));
        if (!stream) {
            return ENOMEM;
        }

        // TODO move to construct function
        usched_mtx_init(&stream->_lock, USCHED_MUTEX_RECURSIVE);
    }
    
    // Reset the stream structure
    stream->_ptr      = stream->_base = NULL;
    stream->_cnt      = 0;
    stream->_fd       = handle->fd;
    stream->_flag     = (int)stream_flags;
    stream->_tmpfname = NULL;
    
    // associate the stream object
    handle->buffered_stream = stream;
    return EOK;
}

int stdio_handle_destroy(stdio_handle_t* handle, int flags)
{
    if (!handle) {
        return EBADF;
    }

    hashtable_remove(&g_stdioObjects, &(struct stdio_object_entry) { .id = handle->fd });
    stdio_bitmap_free(handle->fd);
    free(handle);
    return EOK;
}

int stdio_handle_activity(stdio_handle_t* handle , int activity)
{
    oserr_t status = OSNotificationQueuePost(handle->object.handle, activity);
    if (status != OS_EOK) {
        OsErrToErrNo(status);
        return -1;
    }
    return 0;
}

void stdio_handle_flag(stdio_handle_t* handle, unsigned int flag)
{
    assert(handle != NULL);
    handle->wxflag |= flag;
}

stdio_handle_t* stdio_handle_get(int iod)
{
    struct stdio_object_entry* result;

    result = hashtable_get(&g_stdioObjects, &(struct stdio_object_entry) { .id = iod });
    if (result != NULL) {
        return result->handle;
    }
    return NULL;
}

FILE* __get_std_handle(int n)
{
    switch (n) {
        case STDOUT_FILENO: {
            return &g_stdout;
        }
        case STDIN_FILENO: {
            return &g_stdint;
        }
        case STDERR_FILENO: {
            return &g_stderr;
        }
        default: {
            return NULL;
        }
    }
}

int isatty(int fd)
{
    stdio_handle_t* handle = stdio_handle_get(fd);
    if (!handle) {
        return EBADF;
    }
    return (handle->wxflag & WX_TTY) != 0;
}

hashtable_t* stdio_get_handles(void)
{
    return &g_stdioObjects;
}

uuid_t GetNativeHandle(int iod)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    if (!handle) {
        return UUID_INVALID;
    }
    return handle->object.handle;
}

static uint64_t stdio_hash(const void* element)
{
    const struct stdio_object_entry* entry = element;
    return (uint64_t)entry->id;
}

static int stdio_cmp(const void* element1, const void* element2)
{
    const struct stdio_object_entry* entry1 = element1;
    const struct stdio_object_entry* entry2 = element2;
    return entry1->id == entry2->id ? 0 : -1;
}

void __CleanupSTDIO(void)
{
    // flush all file buffers and close handles
    io_buffer_flush_all(_IOWRT | _IOREAD);

    // close all handles that are not marked _PRIO, and then lastly
    // close the _PRIO handles
    __close_io_descriptors(WX_PRIORITY);
    __close_io_descriptors(0);
}

void StdioInitialize(void)
{
    // initialize the hashtable of handles
    hashtable_construct(
            &g_stdioObjects,
            0,
            sizeof(struct stdio_object_entry),
            stdio_hash, stdio_cmp
    );

    // initialize subsystems for stdio
    stdio_bitmap_initialize();

    // register the cleanup handler
    atexit(__CleanupSTDIO);
}
#endif
