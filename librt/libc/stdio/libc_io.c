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
 * C Standard Library
 * - Standard IO Support functions
 */

#ifdef LIBC_KERNEL
#include <os/spinlock.h>
#include <threading.h>
#include <threads.h>
#include <stdio.h>

spinlock_t __GlbPrintLock = _SPN_INITIALIZER_NP(spinlock_plain);
FILE __GlbStdout = { 0 }, __GlbStdin = { 0 }, __GlbStderr = { 0 };

OsStatus_t
_lock_file(
    _In_ FILE *file)
{
    if (!(file->_flag & _IOSTRG)) {
        spinlock_acquire(&__GlbPrintLock);
    }
    return OsSuccess;
}

OsStatus_t
_unlock_file(
    _In_ FILE *file)
{
    if (!(file->_flag & _IOSTRG)) {
        spinlock_release(&__GlbPrintLock);
    }
    return OsSuccess;
}

FILE *
stdio_get_std(
    _In_ int n)
{
    switch (n) {
        case STDOUT_FILENO: {
            return &__GlbStdout;
        }
        case STDIN_FILENO: {
            return &__GlbStdin;
        }
        case STDERR_FILENO: {
            return &__GlbStderr;
        }
        default: {
            return NULL;
        }
    }
}

int wctomb(char *mbchar, wchar_t wchar)
{
    _CRT_UNUSED(mbchar);
    _CRT_UNUSED(wchar);
    return 0;
}

thrd_t thrd_current(void) {
    return (thrd_t)GetCurrentThreadId();
}

#else
//#define __TRACE
#include <internal/_syscalls.h>
#include <ds/collection.h>
#include <ddk/ipc/ipc.h>
#include <ddk/utils.h>
#include <os/input.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <io.h>
#include "libc_io.h"

static Collection_t IoObjects = COLLECTION_INIT(KeyInteger);
static FILE         __GlbStdout = { 0 }, __GlbStdin = { 0 }, __GlbStderr = { 0 };

/* StdioIsHandleInheritable
 * Returns whether or not the handle should be inheritted by sub-processes based on the requested
 * startup information and the handle settings. */
static OsStatus_t
StdioIsHandleInheritable(
    _In_ ProcessStartupInformation_t*   StartupInformation,
    _In_ stdio_object_t*                 Object)
{
    OsStatus_t Status = OsSuccess;

    if (Object->wxflag & WX_DONTINHERIT) {
        Status = OsError;
    }

    // If we didn't request to inherit one of the handles, then we don't account it
    // for being the one requested.
    if (Object->fd == StartupInformation->StdOutHandle && 
        !(StartupInformation->InheritFlags & PROCESS_INHERIT_STDOUT)) {
        Status = OsError;
    }
    else if (Object->fd == StartupInformation->StdInHandle && 
        !(StartupInformation->InheritFlags & PROCESS_INHERIT_STDIN)) {
        Status = OsError;
    }
    else if (Object->fd == StartupInformation->StdErrHandle && 
        !(StartupInformation->InheritFlags & PROCESS_INHERIT_STDERR)) {
        Status = OsError;
    }
    else if (!(StartupInformation->InheritFlags & PROCESS_INHERIT_FILES)) {
        if (Object->fd != StartupInformation->StdOutHandle &&
            Object->fd != StartupInformation->StdInHandle &&
            Object->fd != StartupInformation->StdErrHandle) {
            Status = OsError;
        }
    }
    return Status;
}

/* StdioGetNumberOfInheritableHandles 
 * Retrieves the count of inheritable filedescriptor handles. This includes both pipes and files. */
static size_t
StdioGetNumberOfInheritableHandles(
    _In_ ProcessStartupInformation_t* StartupInformation)
{
    size_t NumberOfFiles = 0;
    LOCK_FILES();
    foreach(Node, &IoObjects) {
        stdio_object_t* Object = (stdio_object_t*)Node->Data;
        if (StdioIsHandleInheritable(StartupInformation, Object) == OsSuccess) {
            NumberOfFiles++;
        }
    }
    UNLOCK_FILES();
    return NumberOfFiles;
}

/* StdioCreateInheritanceBlock
 * Creates a block of data containing all the stdio handles that can be inherited. */
static OsStatus_t
StdioCreateInheritanceBlock(
    _In_  ProcessStartupInformation_t* StartupInformation,
    _Out_ void**                       InheritationBlock,
    _Out_ size_t*                      InheritationBlockLength)
{
    stdio_object_t*  BlockPointer    = NULL;
    size_t          NumberOfObjects = 0;

    assert(StartupInformation != NULL);

    if (StartupInformation->InheritFlags == PROCESS_INHERIT_NONE) {
        return OsSuccess;
    }
    
    NumberOfObjects = StdioGetNumberOfInheritableHandles(StartupInformation);
    if (NumberOfObjects != 0) {
        *InheritationBlockLength = NumberOfObjects * sizeof(stdio_object_t);
        *InheritationBlock       = malloc(NumberOfObjects * sizeof(stdio_object_t));
        BlockPointer             = (stdio_object_t*)(*InheritationBlock);

        LOCK_FILES();
        foreach(Node, &IoObjects) {
            stdio_object_t* Object = (stdio_object_t*)Node->Data;
            if (StdioIsHandleInheritable(StartupInformation, Object) == OsSuccess) {
                memcpy(BlockPointer, Object, sizeof(stdio_object_t));
                
                // Check for this fd to be equal to one of the custom handles
                // if it is equal, we need to update the fd of the handle to our reserved
                if (Object->fd == StartupInformation->StdOutHandle) {
                    BlockPointer->fd = STDOUT_FILENO;
                }
                if (Object->fd == StartupInformation->StdInHandle) {
                    BlockPointer->fd = STDIN_FILENO;
                }
                if (Object->fd == StartupInformation->StdErrHandle) {
                    BlockPointer->fd = STDERR_FILENO;
                }
                BlockPointer++;
            }
        }
        UNLOCK_FILES();
    }
    return OsSuccess;
}

/* StdioInheritObject
 * Inherits the given object that's been parsed from an inheritance block */
static void
StdioInheritObject(
    _In_ stdio_object_t* Object)
{
    stdio_object_t* object;
    int             status;
    
    status = stdio_object_create(Object->fd, Object->wxflag | WX_INHERITTED, &object);
    if (!status) {
        if (object->fd == STDOUT_FILENO) {
            __GlbStdout._fd = object->fd;
        }
        else if (object->fd == STDIN_FILENO) {
            __GlbStdin._fd = object->fd;
        }
        else if (object->fd == STDERR_FILENO) {
            __GlbStderr._fd = object->fd;
        }
        stdio_object_set_ops_type(object, Object->handle.InheritationType);
        stdio_object_set_handle(object, Object->handle.InheritationHandle);
    }
    else {
        WARNING(" > failed to inherit fd %i", Object->fd);
    }
}

/* StdioParseInheritanceBlock
 * Parses the inheritance block for stdio-objects that should be inheritted from the spawner. */
static void 
StdioParseInheritanceBlock(
    _In_ void*  InheritanceBlock,
    _In_ size_t InheritanceBlockLength)
{
    stdio_object_t* object_out;
    stdio_object_t* object_in;
    stdio_object_t* object_err;
    
    // Handle inheritance
    if (InheritanceBlock != NULL) {
        stdio_object_t* ObjectPointer = (stdio_object_t*)InheritanceBlock;
        size_t         BytesLeft     = InheritanceBlockLength;
        while (BytesLeft >= sizeof(stdio_object_t)) {
            StdioInheritObject(ObjectPointer);
            BytesLeft -= sizeof(stdio_object_t);
            ObjectPointer++;
        }
    }

    // Make sure all default handles have been set for std
    object_out = stdio_object_get(STDOUT_FILENO);
    if (object_out == NULL) {
        stdio_object_create(STDOUT_FILENO, WX_PIPE | WX_TTY, &object_out);
    }
    
    object_in = stdio_object_get(STDIN_FILENO);
    if (object_in == NULL) {
        stdio_object_create(STDIN_FILENO, WX_PIPE | WX_TTY, &object_in);
    }
    
    object_err = stdio_object_get(STDERR_FILENO);
    if (object_err == NULL) {
        stdio_object_create(STDERR_FILENO, WX_PIPE | WX_TTY, &object_out);
    }
    
    stdio_object_set_buffered(object_out, &__GlbStdout, _IOWRT);
    stdio_object_set_buffered(object_in,  &__GlbStdin,  _IOREAD);
    stdio_object_set_buffered(object_err, &__GlbStderr, _IOWRT);
}

static int
stdio_close_all_handles(void)
{
    stdio_object_t* Object;
    int             FilesClosed = 0;
    
    LOCK_FILES();
    while (CollectionBegin(&IoObjects) != NULL) {
        CollectionItem_t* Node = CollectionBegin(&IoObjects);
        Object = (stdio_object_t*)Node->Data;
        
        // Is it a buffered stream or raw?
        if (Object->buffered_stream) {
            fclose(Object->buffered_stream);
        }
        else {
            Object->ops.close(&Object->handle, 0);
        }
        FilesClosed++;
    }
    UNLOCK_FILES();
    return FilesClosed;
}

_CRTIMP void
StdioInitialize(
    _In_ void*  InheritanceBlock,
    _In_ size_t InheritanceBlockLength)
{
    stdio_bitmap_initialize();
    StdioParseInheritanceBlock(InheritanceBlock, InheritanceBlockLength);
}

_CRTIMP void
StdioCleanup(void)
{
    // Flush all file buffers and close handles
    os_flush_all_buffers(_IOWRT | _IOREAD);
    stdio_close_all_handles();
}

int stdio_object_create(int fd, int flags, stdio_object_t** object_out)
{
    stdio_object_t* object;
    DataKey_t       key;

    if (fd == -1) {
        fd = stdio_bitmap_allocate(fd);
        if (fd == -1) {
            _set_errno(EMFILE);
            return -1;
        }
    }

    object = (stdio_object_t*)malloc(sizeof(stdio_object_t));
    if (object) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    object->fd                      = fd;
    object->handle.InheritationType = STDIO_HANDLE_INVALID;
    
    object->wxflag          = WX_OPEN | flags;
    object->lookahead[0]    = '\n';
    object->lookahead[1]    = '\n';
    object->lookahead[2]    = '\n';
    object->buffered_stream = NULL;
    spinlock_init(&object->lock, spinlock_recursive);
    stdio_get_null_operations(&object->ops);

    key.Value.Integer = fd;
    CollectionAppend(&IoObjects, CollectionCreateNode(key, object));
    TRACE(" >> success %i", fd);
    return EOK;
}

int stdio_object_set_handle(stdio_object_t* object, UUId_t handle)
{
    if (!object) {
        return EBADF;
    }
    object->handle.InheritationHandle = handle;
    return EOK;
}

int stdio_object_set_ops_type(stdio_object_t* object, int type)
{
    if (!object) {
        return EBADF;
    }
    
    // Get io operations
    switch (type) {
        case STDIO_HANDLE_PIPE: {
            stdio_get_pipe_operations(&object->ops);
        } break;
        case STDIO_HANDLE_FILE: {
            stdio_get_file_operations(&object->ops);
        } break;
        case STDIO_HANDLE_SOCKET: {
            stdio_get_net_operations(&object->ops);
        } break;
        
        default: {
            stdio_get_null_operations(&object->ops);
        } break;
    }
    return EOK;
}

int stdio_object_set_buffered(stdio_object_t* object, FILE* stream, unsigned int stream_flags)
{
    if (!object) {
        return EBADF;
    }
    
    if (!stream) {
        stream = (FILE*)malloc(sizeof(FILE));
        if (!stream) {
            return ENOMEM;
        }
    }
    
    // Reset the stream structure
    stream->_ptr      = stream->_base = NULL;
    stream->_cnt      = 0;
    stream->_fd       = object->fd;
    stream->_flag     = stream_flags;
    stream->_tmpfname = NULL;
    
    // associate the stream object
    object->buffered_stream = stream;
    return EOK;
}

int stdio_object_destroy(stdio_object_t* object, int flags)
{
    DataKey_t key;
    
    if (!object) {
        return EBADF;
    }
    
    key.Value.Integer = object->fd;
    CollectionRemoveByKey(&IoObjects, key);
    stdio_bitmap_free(object->fd);
    free(object);
    return EOK;
}

stdio_object_t* stdio_object_get(int fd)
{
    DataKey_t Key = { .Value.Integer = fd };
    return (stdio_object_t*)CollectionGetDataByKey(&IoObjects, Key, 0);
}

FILE* stdio_get_std(int n)
{
    switch (n) {
        case STDOUT_FILENO: {
            return &__GlbStdout;
        }
        case STDIN_FILENO: {
            return &__GlbStdin;
        }
        case STDERR_FILENO: {
            return &__GlbStderr;
        }
        default: {
            return NULL;
        }
    }
}

int isatty(int fd)
{
    stdio_object_t* object = stdio_object_get(fd);
    if (!object) {
        return EBADF;
    }
    return object->wxflag & WX_TTY;
}

Collection_t* stdio_get_objects(void)
{
    return &IoObjects;
}


extern OsStatus_t GetKeyFromSystemKeyEnUs(SystemKey_t* Key);

/* TranslateSystemKey
 * Performs the translation on the keycode in the system key structure. This fills
 * in the <KeyUnicode> and <KeyAscii> members by translation of the active keymap. */
OsStatus_t
TranslateSystemKey(
    _In_ SystemKey_t* Key)
{
    if (Key->KeyCode != VK_INVALID) {
        return GetKeyFromSystemKeyEnUs(Key);
    }
    return OsError;
}

#endif
