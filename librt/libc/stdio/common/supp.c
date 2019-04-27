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

Spinlock_t __GlbPrintLock = SPINLOCK_INIT(0);
FILE __GlbStdout = { 0 }, __GlbStdin = { 0 }, __GlbStderr = { 0 };

OsStatus_t
_lock_file(
    _In_ FILE *file)
{
    if (!(file->_flag & _IOSTRG)) {
        SpinlockAcquire(&__GlbPrintLock);
    }
    return OsSuccess;
}

OsStatus_t
_unlock_file(
    _In_ FILE *file)
{
    if (!(file->_flag & _IOSTRG)) {
        SpinlockRelease(&__GlbPrintLock);
    }
    return OsSuccess;
}

FILE *
getstdfile(
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
#include <ddk/services/file.h>
#include <os/services/file.h>
#include <os/input.h>
#include "../../threads/tls.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <io.h>
#include "../local.h"

// Must be reentrancy spinlocks (critical sections)
#define LOCK_FILES() do { } while(0)
#define UNLOCK_FILES() do { } while(0)

static Collection_t IoObjects   = COLLECTION_INIT(KeyInteger);
static int*         FdBitmap    = NULL;
static FILE         __GlbStdout = { 0 }, __GlbStdin = { 0 }, __GlbStderr = { 0 };
static Spinlock_t   BitmapLock  = SPINLOCK_INIT(0);

/* StdioCloneHandle 
 * Allocates and initializes a new stdio handle. */
void
StdioCloneHandle(StdioHandle_t *Handle, StdioHandle_t *Original)
{
    Handle->InheritationType   = Original->InheritationType;
    Handle->InheritationHandle = Original->InheritationHandle;
}

/* StdioCreateFileHandle 
 * Initializes the handle as a file handle in the stdio object */ 
void
StdioCreateFileHandle(UUId_t FileHandle, StdioObject_t* Object)
{
    Object->handle.InheritationType   = STDIO_HANDLE_FILE;
    Object->handle.InheritationHandle = FileHandle;
    Object->exflag |= EF_CLOSE;
}

/* StdioCreateFileHandle 
 * Initializes the handle as a pipe handle in the stdio object */ 
OsStatus_t
StdioCreatePipeHandle(UUId_t PipeHandle, StdioObject_t* Object)
{
    Object->handle.InheritationType   = STDIO_HANDLE_PIPE;
    Object->handle.InheritationHandle = PipeHandle;
    if (PipeHandle == UUID_INVALID) {
        if (CreatePipe(PIPE_RAW, &Object->handle.InheritationHandle) != OsSuccess) {
            return OsError;
        }
        Object->exflag |= EF_CLOSE;
    }
    return OsSuccess;
}

/* StdioIsHandleInheritable
 * Returns whether or not the handle should be inheritted by sub-processes based on the requested
 * startup information and the handle settings. */
static OsStatus_t
StdioIsHandleInheritable(
    _In_ ProcessStartupInformation_t*   StartupInformation,
    _In_ StdioObject_t*                 Object)
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
        StdioObject_t* Object = (StdioObject_t*)Node->Data;
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
    StdioObject_t*  BlockPointer    = NULL;
    size_t          NumberOfObjects = 0;

    assert(StartupInformation != NULL);

    if (StartupInformation->InheritFlags == PROCESS_INHERIT_NONE) {
        return OsSuccess;
    }
    
    NumberOfObjects = StdioGetNumberOfInheritableHandles(StartupInformation);
    if (NumberOfObjects != 0) {
        *InheritationBlockLength = NumberOfObjects * sizeof(StdioObject_t);
        *InheritationBlock       = malloc(NumberOfObjects * sizeof(StdioObject_t));
        BlockPointer             = (StdioObject_t*)(*InheritationBlock);

        LOCK_FILES();
        foreach(Node, &IoObjects) {
            StdioObject_t* Object = (StdioObject_t*)Node->Data;
            if (StdioIsHandleInheritable(StartupInformation, Object) == OsSuccess) {
                memcpy(BlockPointer, Object, sizeof(StdioObject_t));
                
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
    _In_ StdioObject_t* Object)
{
    int fd = StdioFdAllocate(Object->fd, Object->wxflag | WX_INHERITTED);
    if (fd != -1) {
        if (fd == STDOUT_FILENO) {
            __GlbStdout._fd = fd;
        }
        else if (fd == STDIN_FILENO) {
            __GlbStdin._fd = fd;
        }
        else if (fd == STDERR_FILENO) {
            __GlbStderr._fd = fd;
        }
        StdioCloneHandle(&get_ioinfo(fd)->handle, &Object->handle);
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
    // Handle inheritance
    if (InheritanceBlock != NULL) {
        StdioObject_t* ObjectPointer = (StdioObject_t*)InheritanceBlock;
        size_t         BytesLeft     = InheritanceBlockLength;
        while (BytesLeft >= sizeof(StdioObject_t)) {
            StdioInheritObject(ObjectPointer);
            BytesLeft -= sizeof(StdioObject_t);
            ObjectPointer++;
        }
    }

    // Make sure all default handles have been set for std
    if (get_ioinfo(STDOUT_FILENO) == NULL) {
        __GlbStdout._fd = StdioFdAllocate(STDOUT_FILENO, WX_PIPE | WX_TTY);
        assert(__GlbStdout._fd != -1);

        StdioCreatePipeHandle(UUID_INVALID, get_ioinfo(STDOUT_FILENO));
    }
    if (get_ioinfo(STDIN_FILENO) == NULL) {
        __GlbStdin._fd = StdioFdAllocate(STDIN_FILENO, WX_PIPE | WX_TTY);
        assert(__GlbStdin._fd != -1);

        StdioCreatePipeHandle(UUID_INVALID, get_ioinfo(STDIN_FILENO));
    }
    if (get_ioinfo(STDERR_FILENO) == NULL) {
        __GlbStderr._fd = StdioFdAllocate(STDERR_FILENO, WX_PIPE | WX_TTY);
        assert(__GlbStderr._fd != -1);

        StdioCreatePipeHandle(UUID_INVALID, get_ioinfo(STDERR_FILENO));
    }
    StdioFdInitialize(&__GlbStdout, __GlbStdout._fd, _IOWRT);
    StdioFdInitialize(&__GlbStdin,  __GlbStdin._fd,  _IOREAD);
    StdioFdInitialize(&__GlbStderr, __GlbStderr._fd, _IOWRT);
}

/* StdioCloseAllHandles
 * Flushes and closes all opened file handles that are not inheritted. */
static int
StdioCloseAllHandles(void)
{
    StdioObject_t*  Object;
    int             FilesClosed = 0;
    FILE*           File;
    
    LOCK_FILES();
    while (CollectionBegin(&IoObjects) != NULL) {
        CollectionItem_t* Node = CollectionBegin(&IoObjects);
        Object  = (StdioObject_t*)Node->Data;
        if (Object->handle.InheritationType == STDIO_HANDLE_PIPE) {
            close(Object->fd);
        }
        else if (Object->handle.InheritationType == STDIO_HANDLE_FILE) {
            File = (FILE*)Object->file;
            if (File != NULL) {
                if (!fclose(File)) {
                    FilesClosed++;
                }
            }
        }
        else {
            CollectionRemoveByNode(&IoObjects, Node);
        }
    }
    UNLOCK_FILES();
    return FilesClosed;
}

/* StdioInitialize
 * Initializes default handles and resources */
_CRTIMP void
StdioInitialize(
    _In_ void*  InheritanceBlock,
    _In_ size_t InheritanceBlockLength)
{
    // Initialize the bitmap of fds
    FdBitmap = (int *)malloc(DIVUP(INTERNAL_MAXFILES, 8));
    memset(FdBitmap, 0, DIVUP(INTERNAL_MAXFILES, 8));

    StdioParseInheritanceBlock(InheritanceBlock, InheritanceBlockLength);
}

/* StdioCleanup
 * Flushes all files open to disk, and then frees any resources 
 * allocated to the open file handles. */
_CRTIMP void
StdioCleanup(void)
{
    // Flush all file buffers and close handles
    os_flush_all_buffers(_IOWRT | _IOREAD);
    StdioCloseAllHandles();
}

/* StdioFdValid
 * Determines the validity of a file-descriptor handle */
OsStatus_t StdioFdValid(int fd) {
    return fd >= 0 && fd < INTERNAL_MAXFILES && (get_ioinfo(fd)->wxflag & WX_OPEN);
}

/* StdioFdAllocate 
 * Allocates a new file descriptor handle from the bitmap.
 * Returns -1 on error. */
int
StdioFdAllocate(
    _In_ int fd,
    _In_ int flag)
{
    StdioObject_t*  Object      = NULL;
    int             result  = -1;
    DataKey_t       Key;
    int             i, j;
    TRACE("StdioFdAllocate(%i)", fd);

    // Trying to allocate a specific fd?
    SpinlockAcquire(&BitmapLock);
    if (fd >= 0) {
        int Block   = fd / (8 * sizeof(int));
        int Offset  = fd % (8 * sizeof(int));
        if (FdBitmap[Block] & (1 << Offset)) {
            result  = -1;
        }
        else {
            FdBitmap[Block] |= (1 << Offset);
            result  = fd;
        }
    }
    else {
        // Iterate the bitmap and find a free fd
        for (i = 0; i < DIVUP(INTERNAL_MAXFILES, (8 * sizeof(int))); i++) {
            for (j = 0; i < (8 * sizeof(int)); j++) {
                if (!(FdBitmap[i] & (1 << j))) {
                    FdBitmap[i] |= (1 << j);
                    result = (i * (8 * sizeof(int))) + j;
                    break;
                }
            }

            // Early breakout?
            if (j != (8 * sizeof(int))) {
                break;
            }
        }
    }
    SpinlockRelease(&BitmapLock);

    // Create a new io-object
    if (result != -1) {
        Object      = (StdioObject_t*)malloc(sizeof(StdioObject_t));
        Object->fd  = result;

        Object->handle.InheritationType = STDIO_HANDLE_INVALID;
        
        Object->wxflag       = WX_OPEN | flag;
        Object->lookahead[0] = '\n';
        Object->lookahead[1] = '\n';
        Object->lookahead[2] = '\n';
        Object->exflag       = 0;
        Object->file         = NULL;
        SpinlockReset(&Object->lock, SPINLOCK_RECURSIVE);
    
        // Add to list
        Key.Value.Integer = result;
        CollectionAppend(&IoObjects, CollectionCreateNode(Key, Object));
        TRACE(" >> success %i", fd);
    }
    return result;
}

/* StdioFdFree
 * Frees an already allocated file descriptor handle in the bitmap. */
void
StdioFdFree(
    _In_ int fd)
{
    int         Block;
    int         Offset;
    DataKey_t   Key = { .Value.Integer = fd };
    void*       Object;

    Object = CollectionGetDataByKey(&IoObjects, Key, 0);
    if (Object != NULL) {
        if (CollectionRemoveByKey(&IoObjects, Key) != OsSuccess) {
            ERROR(" > failed to remove io object for fd %i, it may not exist", fd);
        }
        free(Object);
    }

    if (fd > STDERR_FILENO) {
        Block   = fd / (8 * sizeof(int));
        Offset  = fd % (8 * sizeof(int));

        // Set the given fd index to free
        SpinlockAcquire(&BitmapLock);
        FdBitmap[Block] &= ~(1 << Offset);
        SpinlockRelease(&BitmapLock);
    }
}

/* StdioFdToHandle
 * Retrieves the file descriptor os-handle from the given fd */
StdioHandle_t*
StdioFdToHandle(
    _In_ int fd)
{
    CollectionItem_t*   fNode;
    DataKey_t           Key = { .Value.Integer = fd };

    // Free any resources allocated by the fd
    fNode = CollectionGetNodeByKey(&IoObjects, Key, 0);
    if (fNode != NULL) {
        return &((StdioObject_t*)fNode->Data)->handle;
    }
    else {
        return NULL;
    }
}

/* StdioFdInitialize
 * Initializes a FILE stream object with the given file descriptor */
OsStatus_t
StdioFdInitialize(
    _In_ FILE*      file, 
    _In_ int        fd,
    _In_ unsigned   stream_flags)
{
    StdioObject_t*      Object;
    CollectionItem_t*   Node;
    DataKey_t           Key = { .Value.Integer = fd };
    TRACE("StdioFdInitialize(%i)", fd);

    Node = CollectionGetNodeByKey(&IoObjects, Key, 0);
    if (Node != NULL) {
        Object = (StdioObject_t*)Node->Data;
        if (Object->wxflag & WX_OPEN) {
            file->_ptr      = file->_base = NULL;
            file->_cnt      = 0;
            file->_fd       = fd;
            file->_flag     = stream_flags;
            file->_tmpfname = NULL;
            Object->file    = file;
            TRACE(" > success (%i, 0x%" PRIxIN ")", fd, file);
            return OsSuccess;
        }
        else {
            _set_errno(EBADF);
            return OsError;
        }
    }
    else {
        _set_errno(EBADFD);
        return OsError;
    }
}

/* StdioHandleReadFile
 * Reads the requested number of bytes from a file handle */
OsStatus_t
StdioHandleReadFile(
    _In_  StdioHandle_t* Handle, 
    _In_  char*          Buffer, 
    _In_  size_t         Length,
    _Out_ size_t*        BytesRead)
{
    uint8_t *Pointer        = (uint8_t*)Buffer;
    size_t BytesReadTotal   = 0, BytesLeft = Length;
    size_t OriginalSize     = GetBufferSize(tls_current()->transfer_buffer);

    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once. When? Who knows, but in our case anything
    // more than 5 transfers is useless
    if (Length >= (OriginalSize * 5)) {
        DmaBuffer_t *TransferBuffer     = CreateBuffer(UUID_INVALID, Length);
        size_t BytesReadFs              = 0, BytesIndex = 0;
        FileSystemCode_t FsCode;

        FsCode = ReadFile(Handle->InheritationHandle, GetBufferHandle(TransferBuffer), Length, &BytesIndex, &BytesReadFs);
        if (_fval(FsCode) || BytesReadFs == 0) {
            DestroyBuffer(TransferBuffer);
            if (BytesReadFs == 0) {
                *BytesRead = 0;
                return OsSuccess;
            }
            return OsError;
        }

        SeekBuffer(TransferBuffer, BytesIndex);
        ReadBuffer(TransferBuffer, (const void*)Buffer, BytesReadFs, NULL);
        DestroyBuffer(TransferBuffer);
        *BytesRead = BytesReadFs;
        return OsSuccess;
    }
    
    // Keep reading chunks untill we've read all requested
    while (BytesLeft > 0) {
        FileSystemCode_t FsCode = FsOk;
        size_t ChunkSize        = MIN(OriginalSize, BytesLeft);
        size_t BytesReadFs      = 0, BytesIndex = 0;

        // Perform the read
        FsCode = ReadFile(Handle->InheritationHandle, GetBufferHandle(tls_current()->transfer_buffer), 
            ChunkSize, &BytesIndex, &BytesReadFs);
        if (_fval(FsCode) || BytesReadFs == 0) {
            break;
        }
        
        // Seek to the valid buffer index, then read the byte count
        SeekBuffer(tls_current()->transfer_buffer, BytesIndex);
        ReadBuffer(tls_current()->transfer_buffer, (const void*)Pointer, BytesReadFs, NULL);
        SeekBuffer(tls_current()->transfer_buffer, 0);

        // Update indices
        BytesLeft       -= BytesReadFs;
        BytesReadTotal  += BytesReadFs;
        Pointer         += BytesReadFs;
    }

    // Restore transfer buffer
    *BytesRead = BytesReadTotal;
    return OsSuccess;
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

/* ReadSystemKey
 * Reads a system key from the process's stdin handle. This returns
 * the raw system key with no processing performed on the key. */
OsStatus_t
ReadSystemKey(
    _In_ SystemKey_t*   Key)
{
    StdioHandle_t *Handle = StdioFdToHandle(STDIN_FILENO);
    if (Handle->InheritationType == STDIO_HANDLE_FILE) {
        return StdioHandleReadFile(Handle, (char*)Key, sizeof(SystemKey_t), NULL);
    }
    else {
        return ReadPipe(Handle->InheritationHandle, Key, sizeof(SystemKey_t));
    }
}

/* StdioReadInternal
 * Internal read wrapper for file-reading */
OsStatus_t
StdioReadInternal(
    _In_  int           fd, 
    _In_  char*         Buffer, 
    _In_  size_t        Length,
    _Out_ size_t*       BytesRead)
{
    // Variables
    StdioHandle_t *Handle   = StdioFdToHandle(fd);
    SystemKey_t Key;

    if (Handle->InheritationType == STDIO_HANDLE_FILE) {
        return StdioHandleReadFile(Handle, Buffer, Length, BytesRead);
    }
    else if (Handle->InheritationType == STDIO_HANDLE_PIPE) {
        if (fd == STDIN_FILENO) {
            while (Length--) { // @todo handle wide?
                if (ReadSystemKey(&Key) != OsSuccess) {
                    break;   
                }

                // Perform key translation
                TranslateSystemKey(&Key);
                *Buffer++ = (char)Key.KeyAscii;
                (*BytesRead)++;
            }
            return OsSuccess;
        }
        else if (ReadPipe(Handle->InheritationHandle, Buffer, Length) == OsSuccess) {
            *BytesRead = Length;
            return OsSuccess;
        }
        _set_errno(EPIPE);
        return OsError;
    }
    else {
        _set_errno(EBADF);
        return OsError;
    }
}

/* StdioHandleWriteFile
 * Writes the requested number of bytes to a file handle */
OsStatus_t
StdioHandleWriteFile(
    _In_  StdioHandle_t* Handle, 
    _In_  char*          Buffer, 
    _In_  size_t         Length,
    _Out_ size_t*        BytesWritten)
{
    size_t BytesWrittenTotal = 0, BytesLeft = (size_t)Length;
    size_t OriginalSize = GetBufferSize(tls_current()->transfer_buffer);
    uint8_t *Pointer = (uint8_t*)Buffer;

    // Keep writing chunks untill we've read all requested
    while (BytesLeft > 0) {
        size_t ChunkSize = MIN(OriginalSize, BytesLeft);
        size_t BytesWrittenLocal = 0;
        
        SeekBuffer(tls_current()->transfer_buffer, 0); // Rewind buffer
        WriteBuffer(tls_current()->transfer_buffer, (const void *)Pointer, ChunkSize, &BytesWrittenLocal);
        if (WriteFile(Handle->InheritationHandle, GetBufferHandle(tls_current()->transfer_buffer), 
            ChunkSize, &BytesWrittenLocal) != FsOk) {
            break;
        }
        if (BytesWrittenLocal == 0) {
            break;
        }
        BytesWrittenTotal += BytesWrittenLocal;
        BytesLeft -= BytesWrittenLocal;
        Pointer += BytesWrittenLocal;
    }

    // Restore our transfer buffer and return
    *BytesWritten = BytesWrittenTotal;
    return OsSuccess;
}

/* StdioWriteInternal
 * Internal write wrapper for file-writing */
OsStatus_t
StdioWriteInternal(
    _In_  int       fd, 
    _In_  char*     Buffer, 
    _In_  size_t    Length,
    _Out_ size_t*   BytesWritten)
{
    StdioHandle_t* Handle = StdioFdToHandle(fd);
    assert(Handle != NULL);

    if (Handle->InheritationType == STDIO_HANDLE_FILE) {
        return StdioHandleWriteFile(Handle, Buffer, Length, BytesWritten);
    }
    else if (Handle->InheritationType == STDIO_HANDLE_PIPE) {
        if (WritePipe(Handle->InheritationHandle, Buffer, Length) == OsSuccess) {
            *BytesWritten = Length;
            return OsSuccess;
        }
        _set_errno(EPIPE);
        return OsError;
    }
    else {
        _set_errno(EBADF);
        return OsError;
    }
}

/* StdioHandleSeekFile 
 * Performs a seek or a tell operation on a file handle */
OsStatus_t
StdioHandleSeekFile(
    _In_  StdioHandle_t*    Handle,
    _In_  off64_t           Offset,
    _In_  int               Origin,
    _Out_ long long*        Position)
{
    FileSystemCode_t    FsStatus;
    LargeInteger_t      SeekFinal;
    OsStatus_t          Status;

    // If we search from SEEK_SET, just build offset directly
    if (Origin != SEEK_SET) {
        LargeInteger_t FileInitial;

        // Adjust for seek origin
        if (Origin == SEEK_CUR) {
            Status = GetFilePosition(Handle->InheritationHandle, &FileInitial.u.LowPart, &FileInitial.u.HighPart);
            if (Status != OsSuccess) {
                ERROR("failed to get file position");
                return OsError;
            }

            // Sanitize for overflow
            if ((size_t)FileInitial.QuadPart != FileInitial.QuadPart) {
                ERROR("file-offset-overflow");
                _set_errno(EOVERFLOW);
                return OsError;
            }
        }
        else {
            Status = GetFileSize(Handle->InheritationHandle, &FileInitial.u.LowPart, &FileInitial.u.HighPart);
            if (Status != OsSuccess) {
                ERROR("failed to get file size");
                return OsError;
            }
        }
        SeekFinal.QuadPart = FileInitial.QuadPart + Offset;
    }
    else {
        SeekFinal.QuadPart = Offset;
    }

    // Now perform the seek
    FsStatus = SeekFile(Handle->InheritationHandle, SeekFinal.u.LowPart, SeekFinal.u.HighPart);
    if (!_fval((int)FsStatus)) {
        *Position = SeekFinal.QuadPart;
        return OsSuccess;
    }
    TRACE("stdio::fseek::fail %u", FsStatus);
    return OsError;
}

/* StdioSeekInternal
 * Internal wrapper for stdio's syntax, conversion to our own RPC syntax */
OsStatus_t
StdioSeekInternal(
    _In_  int           fd,
    _In_  off64_t       Offset,
    _In_  int           Origin,
    _Out_ long long*    Position)
{
    StdioHandle_t *Handle   = StdioFdToHandle(fd);

    if (Handle->InheritationType == STDIO_HANDLE_FILE) {
        return StdioHandleSeekFile(Handle, Offset, Origin, Position);
    }
    else if (Handle->InheritationType == STDIO_HANDLE_PIPE) {
        _set_errno(EPIPE);
        return OsError;
    }
    else {
        _set_errno(EBADF);
        return OsError;
    }
}

/* getstdfile
 * Retrieves a standard io stream handle */
FILE* getstdfile(int n)
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

/* os_flush_buffer
 * Flushes the number fo bytes stored in the buffer and resets
 * the buffer to initial state */
OsStatus_t
os_flush_buffer(
    _In_ FILE* file)
{
    if ((file->_flag & (_IOREAD | _IOWRT)) == _IOWRT && 
        file->_flag & (_IOMYBUF | _USERBUF)) {
        int cnt = file->_ptr - file->_base;

        // Flush them
        if (cnt > 0 && write(file->_fd, file->_base, cnt) != cnt) {
            file->_flag |= _IOERR;
            return OsError;
        }

        // If it's rw, clear the write flag
        if (file->_flag & _IORW) {
            file->_flag &= ~_IOWRT;
        }
        file->_ptr = file->_base;
        file->_cnt = 0;
    }
    return OsSuccess;
}

/* os_flush_all_buffers
 * Flush all stream buffer */
int
os_flush_all_buffers(
    _In_ int mask)
{
    StdioObject_t*  Object;
    int             FilesFlushes = 0;
    FILE*           File;

    LOCK_FILES();
    foreach(Node, &IoObjects) {
        Object  = (StdioObject_t*)Node->Data;
        File    = (FILE*)Object->file;
        if (File != NULL && (File->_flag & mask)) {
            fflush(File);
            FilesFlushes++;
        }
    }
    UNLOCK_FILES();
    return FilesFlushes;
}

/* get_ioinfo
 * Retrieves the io-object that is bound to the given file descriptor. */
StdioObject_t*
get_ioinfo(
    _In_ int fd)
{
    DataKey_t Key = { .Value.Integer = fd };
    return (StdioObject_t*)CollectionGetDataByKey(&IoObjects, Key, 0);
}

/* isatty
 * Returns non-zero if the given file-descriptor points to a tty. */
int isatty(int fd) {
    return get_ioinfo(fd)->wxflag & WX_TTY;
}

/* os_alloc_buffer
 * Allocates a transfer buffer for a stdio file stream */
OsStatus_t
os_alloc_buffer(
    _In_ FILE *file)
{
    // Sanitize that it's not an std tty stream
    if ((file->_fd == STDOUT_FILENO || file->_fd == STDERR_FILENO) && isatty(file->_fd)) {
        return OsError;
    }

    // Allocate a transfer buffer
    file->_base = calloc(1, INTERNAL_BUFSIZ);
    if (file->_base) {
        file->_bufsiz = INTERNAL_BUFSIZ;
        file->_flag |= _IOMYBUF;
    }
    else {
        file->_base = (char *)(&file->_charbuf);
        file->_bufsiz = 2;
        file->_flag |= _IONBF;
    }

    // Update pointer to base and 0 count
    file->_ptr = file->_base;
    file->_cnt = 0;
    return OsSuccess;
}

/* add_std_buffer
 * Allocate temporary buffer for stdout and stderr */
OsStatus_t
add_std_buffer(
    _In_ FILE *file)
{
    // Static write buffers
    static char buffers[2][BUFSIZ];

    // Sanitize the file stream
    if ((file->_fd != STDOUT_FILENO && file->_fd != STDERR_FILENO) 
        || (file->_flag & (_IONBF | _IOMYBUF | _USERBUF)) 
        || !isatty(file->_fd)) {
        return OsError;
    }

    // Update buffer pointers
    file->_ptr = file->_base =
        buffers[file->_fd == STDOUT_FILENO ? 0 : 1];
    file->_bufsiz = file->_cnt = BUFSIZ;
    file->_flag |= _USERBUF;
    return OsSuccess;
}

/* remove_std_buffer
 * Removes temporary buffer from stdout or stderr
 * Only call this function when add_std_buffer returned Success */
void
remove_std_buffer(
    _In_ FILE *file)
{
    os_flush_buffer(file);
    file->_ptr    = file->_base = NULL;
    file->_bufsiz = file->_cnt = 0;
    file->_flag   &= ~_USERBUF;
}

OsStatus_t
_lock_file(
    _In_ FILE *file)
{
    TRACE("_lock_file(0x%" PRIxIN ")", file);
    if (!(file->_flag & _IOSTRG)) {
        assert(get_ioinfo(file->_fd) != NULL);
        SpinlockAcquire(&get_ioinfo(file->_fd)->lock);
    }
    return OsSuccess;
}

OsStatus_t
_unlock_file(
    _In_ FILE *file)
{
    TRACE("_unlock_file(0x%" PRIxIN ")", file);
    if (!(file->_flag & _IOSTRG)) {
        assert(get_ioinfo(file->_fd) != NULL);
        SpinlockRelease(&get_ioinfo(file->_fd)->lock);
    }
    return OsSuccess;
}

#endif
