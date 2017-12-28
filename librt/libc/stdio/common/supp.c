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
 * - Standard IO Support functions
 */

#ifdef LIBC_KERNEL
#include <os/spinlock.h>
#include <stdio.h>

/* Globals
 * Static initialization for kernel outs */
Spinlock_t __GlbPrintLock = SPINLOCK_INIT;
FILE __GlbStdout = { 0 }, __GlbStdin = { 0 }, __GlbStderr = { 0 };

/* _lock_file
 * Performs primitive locking on a file-stream. */
OsStatus_t
_lock_file(
    _In_ FILE *file)
{
    if (!(file->_flag & _IOSTRG)) {
        return SpinlockAcquire(&__GlbPrintLock);
    }
    return OsSuccess;
}

/* _unlock_file
 * Performs primitive unlocking on a file-stream. */
OsStatus_t
_unlock_file(
    _In_ FILE *file)
{
    if (!(file->_flag & _IOSTRG)) {
        return SpinlockRelease(&__GlbPrintLock);
    }
    return OsSuccess;
}

/* getstdfile
 * Retrieves a standard io stream handle */
FILE *
getstdfile(
    _In_ int n)
{
    switch (n) {
        case STDOUT_FD: {
            return &__GlbStdout;
        }
        case STDIN_FD: {
            return &__GlbStdin;
        }
        case STDERR_FD: {
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

#else
#include <os/driver/input.h>
#include <os/driver/file.h>
#include <os/ipc/ipc.h>
#include <ds/collection.h>
#include "../../threads/tls.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <io.h>
#include "../local.h"

// Must be reentrancy spinlocks (critical sections)
#define LOCK_FILES() do { } while(0)
#define UNLOCK_FILES() do { } while(0)

/* Globals
 * Used to keep state of all io-objects */
static Collection_t *IoObjects  = NULL;
static int *FdBitmap            = NULL;
static Spinlock_t BitmapLock;
static Spinlock_t IoLock;
FILE __GlbStdout, __GlbStdin, __GlbStderr;

/* Prototypes
 * Forward-declare these */
int _flushall(void);
int _fcloseall(void);

/* StdioInitialize
 * Initializes default handles and resources */
_CRTIMP
void
StdioInitialize(void)
{
    // Initialize the list of io-objects
    IoObjects = CollectionCreate(KeyInteger);
    SpinlockReset(&IoLock);

    // Initialize the bitmap of fds
    FdBitmap = (int *)malloc(DIVUP(INTERNAL_MAXFILES, 8));
    memset(FdBitmap, 0, DIVUP(INTERNAL_MAXFILES, 8));
    SpinlockReset(&BitmapLock);

    // Initialize the STDOUT handle
    memset(&__GlbStdout, 0, sizeof(FILE));
    __GlbStdout._fd = StdioFdAllocate(UUID_INVALID, WX_PIPE | WX_TTY);
    StdioFdInitialize(&__GlbStdout, __GlbStdout._fd, _IOWRT);

    // Initialize the STDIN handle
    memset(&__GlbStdin, 0, sizeof(FILE));
    __GlbStdin._fd = StdioFdAllocate(UUID_INVALID, WX_PIPE | WX_TTY);
    StdioFdInitialize(&__GlbStdin, __GlbStdin._fd, _IOREAD);

    // Initialize the STDERR handle
    memset(&__GlbStderr, 0, sizeof(FILE));
    __GlbStderr._fd = StdioFdAllocate(UUID_INVALID, WX_PIPE | WX_TTY);
    StdioFdInitialize(&__GlbStderr, __GlbStderr._fd, _IOWRT);
}

/* StdioCleanup
 * Flushes all files open to disk, and then frees any resources 
 * allocated to the open file handles. */
_CRTIMP
void
StdioCleanup(void)
{
    // Flush all file buffers and close handles
    _flushall();
    _fcloseall();
}

/* StdioFdValid
 * Determines the validity of a file-descriptor handle */
OsStatus_t
StdioFdValid(
    _In_ int fd)
{
    return fd >= 0 && fd < INTERNAL_MAXFILES 
        && (get_ioinfo(fd)->wxflag & WX_OPEN);
}

/* StdioFdAllocate 
 * Allocates a new file descriptor handle from the bitmap.
 * Returns -1 on error. */
int
StdioFdAllocate(
    _In_ UUId_t handle, 
    _In_ int flag)
{
    // Variables
    ioobject *io = NULL;
    DataKey_t Key;
    int i, j, result = -1;

    // Acquire the bitmap lock
    SpinlockAcquire(&BitmapLock);

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

    // Release lock before returning
    SpinlockRelease(&BitmapLock);

    // Create a new io-object
    if (result != -1) {
        io = (ioobject*)malloc(sizeof(ioobject));
        io->handle = handle;
        io->wxflag = WX_OPEN | (flag & (WX_DONTINHERIT | WX_APPEND | WX_TEXT | WX_PIPE | WX_TTY));
        io->lookahead[0] = '\n';
        io->lookahead[1] = '\n';
        io->lookahead[2] = '\n';
        io->exflag = 0;
    
        // Add to list
        Key.Value = result;
        SpinlockAcquire(&IoLock);
        CollectionAppend(IoObjects, CollectionCreateNode(Key, io));
        SpinlockRelease(&IoLock);
    }

    // Done
    return result;
}

/* StdioFdFree
 * Frees an already allocated file descriptor handle in the bitmap. */
void
StdioFdFree(
    _In_ int fd)
{
    // Variables
    CollectionItem_t *fNode = NULL;
    DataKey_t Key;

    // Free any resources allocated by the fd
    Key.Value = fd;
    SpinlockAcquire(&IoLock);
    fNode = CollectionGetNodeByKey(IoObjects, Key, 0);
    if (fNode != NULL) {
        free(fNode->Data);
        CollectionRemoveByNode(IoObjects, fNode);
        CollectionDestroyNode(IoObjects, fNode);
    }
    SpinlockRelease(&IoLock);

    // Set the given fd index to free
    SpinlockAcquire(&BitmapLock);
    FdBitmap[fd / (8 * sizeof(int))] &=
        ~(1 << (fd % (8 * sizeof(int))));
    SpinlockRelease(&BitmapLock);
}

/* StdioFdToHandle
 * Retrieves the file descriptor os-handle from the given fd */
UUId_t
StdioFdToHandle(
    _In_ int fd)
{
    // Variables
    CollectionItem_t *fNode = NULL;
    DataKey_t Key;

    // Free any resources allocated by the fd
    Key.Value = fd;
    SpinlockAcquire(&IoLock);
    fNode = CollectionGetNodeByKey(IoObjects, Key, 0);
    SpinlockRelease(&IoLock);
    if (fNode != NULL) {
        return ((ioobject*)fNode->Data)->handle;
    }
    else {
        return UUID_INVALID;
    }
}

/* StdioFdInitialize
 * Initializes a FILE stream object with the given file descriptor */
OsStatus_t
StdioFdInitialize(
    _In_ FILE *file, 
    _In_ int fd,
    _In_ unsigned stream_flags)
{
    // Variables
    CollectionItem_t *fNode = NULL;
    DataKey_t Key;

    // Lookup node of the file descriptor
    Key.Value = fd;
    SpinlockAcquire(&IoLock);
    fNode = CollectionGetNodeByKey(IoObjects, Key, 0);
    SpinlockRelease(&IoLock);
    
    // Node must exist
    if (fNode != NULL) {
        if (((ioobject*)fNode->Data)->wxflag & WX_OPEN) {
            file->_ptr = file->_base = NULL;
            file->_cnt = 0;
            file->_fd = fd;
            file->_flag = stream_flags;
            file->_tmpfname = NULL;
            ((ioobject*)fNode->Data)->file = file;
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

/* StdioReadStdin
 * Waits for an input event of type key, and returns the key.
 * This is a blocking call. */
int
StdioReadStdin(void)
{
	// Variables
	MRemoteCall_t Event;
    char EventBuffer[IPC_MAX_MESSAGELENGTH];
	MInput_t *Input     = NULL;
	int Character       = 0;
	int Run             = 1;

	// Wait for input message, we need to discard 
	// everything else as this is a polling op
	while (Run) {
		if (RPCListen(&Event, &EventBuffer[0]) == OsSuccess) {
			Input = (MInput_t*)Event.Arguments[0].Data.Buffer;
			if (Event.Function == EVENT_INPUT) {
				if (Input->Type == InputKeyboard
					&& (Input->Flags & INPUT_BUTTON_CLICKED)) {
					Character = (int)Input->Key;
					Run = 0;
				}
			}
		}
	}

	// Return the resulting read character
	return Character;
}

/* StdioReadInternal
 * Internal read wrapper for file-reading */
OsStatus_t
StdioReadInternal(
    _In_ int fd, 
    _In_ char *Buffer, 
    _In_ size_t Length,
    _Out_ size_t *BytesRead)
{
    // Variables
	size_t BytesReadTotal = 0, BytesLeft = (size_t)Length;
	size_t OriginalSize = GetBufferSize(tls_current()->transfer_buffer);
	uint8_t *Pointer = (uint8_t*)Buffer;
    UUId_t Handle = StdioFdToHandle(fd);
    
    // Determine handle
    if (Handle == UUID_INVALID) {
        // Only one case this is allowed
        if (__GlbStdin._fd == fd) {
            switch (BytesLeft) {
                case 1: {
                    *Pointer = (uint8_t)StdioReadStdin();
                    *BytesRead = 1;
                } break;
                case 2: {
                    *((uint16_t*)Pointer) = (uint16_t)StdioReadStdin();
                    *BytesRead = 2;
                } break;
                case 4: {
                    *((uint32_t*)Pointer) = (uint32_t)StdioReadStdin();
                    *BytesRead = 4;
                } break;
                default: {
                    return OsError;
                }
            }
            return OsSuccess;
        }

        // Otherwise set error
        _set_errno(EBADF);
		return OsError;
    }

	// Keep reading chunks untill we've read all requested
	while (BytesLeft > 0) {
		size_t ChunkSize = MIN(OriginalSize, BytesLeft);
		size_t BytesReaden = 0, BytesIndex = 0;
		ChangeBufferSize(tls_current()->transfer_buffer, ChunkSize);
        if (_fval(ReadFile(Handle, tls_current()->transfer_buffer, 
            &BytesIndex, &BytesReaden))) {
			break;
		}
		if (BytesReaden == 0) {
			break;
		}
		SeekBuffer(tls_current()->transfer_buffer, BytesIndex);
        ReadBuffer(tls_current()->transfer_buffer, 
            (__CONST void*)Pointer, BytesReaden, NULL);
		SeekBuffer(tls_current()->transfer_buffer, 0);
		BytesReadTotal += BytesReaden;
		BytesLeft -= BytesReaden;
		Pointer += BytesReaden;
	}

    // Restore transfer buffer
    *BytesRead = BytesReadTotal;
	return ChangeBufferSize(tls_current()->transfer_buffer, OriginalSize);
}

/* StdioWriteInternal
 * Internal write wrapper for file-writing */
OsStatus_t
StdioWriteInternal(
    _In_ int fd, 
    _Out_ char *Buffer, 
    _In_ size_t Length,
    _Out_ size_t *BytesWritten)
{
    // Variables
	size_t BytesWrittenTotal = 0, BytesLeft = (size_t)Length;
	size_t OriginalSize = GetBufferSize(tls_current()->transfer_buffer);
    uint8_t *Pointer = (uint8_t *)Buffer;
    UUId_t Handle = StdioFdToHandle(fd);
    
    // Special cases
    if (Handle == UUID_INVALID) {
        // Check for stdout
        if (__GlbStdout._fd == fd) {

        }
        else if (__GlbStderr._fd == fd) {

        }

        // Otherwise set error
        _set_errno(EBADF);
		return OsError;
    }

	// Keep writing chunks untill we've read all requested
	while (BytesLeft > 0) {
		size_t ChunkSize = MIN(OriginalSize, BytesLeft);
		size_t BytesWrittenLocal = 0;
		ChangeBufferSize(tls_current()->transfer_buffer, ChunkSize);
        WriteBuffer(tls_current()->transfer_buffer, 
            (__CONST void *)Pointer, ChunkSize, &BytesWrittenLocal);
		if (WriteFile(Handle, tls_current()->transfer_buffer, &BytesWrittenLocal) != FsOk) {
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
    ChangeBufferSize(tls_current()->transfer_buffer, OriginalSize);
    *BytesWritten = BytesWrittenTotal;
	return OsSuccess;
}

off64_t offabs(off64_t Value) {
	return (Value <= 0) ? 0 - Value : Value;
}

/* StdioSeekInternal
 * Internal wrapper for stdio's syntax, conversion to our own RPC syntax */
OsStatus_t
StdioSeekInternal(
    _In_ int fd, 
    _In_ off64_t Offset, 
    _In_ int Origin,
    _Out_ long long *Position)
{
    // Variables
    off_t SeekSpotLow = 0, SeekSpotHigh = 0;
    UUId_t Handle = StdioFdToHandle(fd);

    // Sanitize the handle
    if (Handle == UUID_INVALID) {
        return OsError;
    }

    // If we search from SEEK_SET, just build offset directly
	if (Origin != SEEK_SET) {
		// Start calculation of corrected offsets
		off64_t CorrectedValue = offabs(Offset);
		uint64_t fPos = 0, fSize = 0;
		uint32_t pLo = 0, pHi = 0, sLo = 0, sHi = 0;

	    // Invoke filemanager services
		if (GetFilePosition(Handle, &pLo, &pHi) != OsSuccess
			&& GetFileSize(Handle, &sLo, &sHi) != OsSuccess) {
			return OsError;
		}
		else {
			fSize = ((uint64_t)sHi << 32) | sLo;
			fPos = ((uint64_t)pHi << 32) | pLo;
		}

		// Sanitize for overflow
		if ((size_t)fPos != fPos) {
			_set_errno(EOVERFLOW);
			return OsError;
		}

		// Adjust for seek origin
		if (Origin == SEEK_CUR) {
			if (Offset < 0) {
				Offset = (long)fPos - CorrectedValue;
			}
			else {
				Offset = (long)fPos + CorrectedValue;
			}
		}
		else {
			Offset = (long)fSize - CorrectedValue;
		}
	}

	// Build the final destination
	SeekSpotLow = Offset & 0xFFFFFFFF;
	SeekSpotHigh = (Offset >> 32) & 0xFFFFFFFF;

	// Now perform the seek
	if (_fval(SeekFile(Handle, SeekSpotLow, SeekSpotHigh))) {
		return OsError;
	}
	else {
        *Position = ((long long)SeekSpotHigh << 32) | SeekSpotLow;
		return OsSuccess;
	}
}

/* getstdfile
 * Retrieves a standard io stream handle */
FILE *
getstdfile(
    _In_ int n)
{
    switch (n) {
        case STDOUT_FD: {
            return &__GlbStdout;
        }
        case STDIN_FD: {
            return &__GlbStdin;
        }
        case STDERR_FD: {
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
    FILE *file)
{
    if ((file->_flag & (_IOREAD | _IOWRT)) == _IOWRT && file->_flag & (_IOMYBUF | _USERBUF))
    {

        // Calculate the number of bytes to write
        int cnt = file->_ptr - file->_base;

        // Flush them
        if (cnt > 0 && _write(file->_fd, file->_base, cnt) != cnt) {
            file->_flag |= _IOERR;
            return OsError;
        }

        // If it's rw, clear the write flag
        if (file->_flag & _IORW) {
            file->_flag &= ~_IOWRT;
        }

        // Reset buffer pointer/pos
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
    int num_flushed = 0;
    FILE *file;

    // Iterate list of open files
    LOCK_FILES();
    foreach(fNode, IoObjects) {
        file = (FILE*)(((ioobject*)fNode->Data)->file);

        // Does file match the given mask?
        if (file->_flag & mask) {
            fflush(file);
            num_flushed++;
        }
    }
    UNLOCK_FILES();

    return num_flushed;
}

/* get_ioinfo
 * Retrieves the io-object that is bound to the given file descriptor. */
ioobject*
get_ioinfo(int fd)
{
    // Variables
    DataKey_t Key;
    Key.Value = fd;

    // Lookup io-object
    return (ioobject*)CollectionGetDataByKey(IoObjects, Key, 0);
}

/* _fcloseall
 * Closes all open streams in this process-scope. */
int
_fcloseall(void)
{
    int num_closed = 0;
    FILE *file;

    LOCK_FILES();
    foreach(fNode, IoObjects) {
        file = (FILE*)(((ioobject*)fNode->Data)->file);
        if (!fclose(file)) {
            num_closed++;      
        }
    }
    UNLOCK_FILES();
    return num_closed;
}

/* _isatty
 * Returns non-zero if the given file-descriptor points to a tty. */
int
_isatty(
    _In_ int fd)
{
    return get_ioinfo(fd)->wxflag & WX_TTY;
}

/* _flushall
 * Flushes all open streams in this process-scope. */
int
_flushall(void)
{
    return os_flush_all_buffers(_IOWRT | _IOREAD);
}

/* os_alloc_buffer
 * Allocates a transfer buffer for a stdio file stream */
OsStatus_t
os_alloc_buffer(
    _In_ FILE *file)
{
    // Sanitize that it's not an std tty stream
    if ((file->_fd == STDOUT_FD || file->_fd == STDERR_FD) && _isatty(file->_fd)) {
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
    if ((file->_fd != STDOUT_FD && file->_fd != STDERR_FD) 
        || (file->_flag & (_IONBF | _IOMYBUF | _USERBUF)) 
        || !_isatty(file->_fd)) {
        return OsError;
    }

    // Update buffer pointers
    file->_ptr = file->_base =
        buffers[file->_fd == STDOUT_FD ? 0 : 1];
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
    // Flush it first
    os_flush_buffer(file);

    // Remove buffer
    file->_ptr = file->_base = NULL;
    file->_bufsiz = file->_cnt = 0;
    file->_flag &= ~_USERBUF;
}

/* _lock_file
 * Performs primitive locking on a file-stream. */
OsStatus_t
_lock_file(
    _In_ FILE *file)
{
    if (!(file->_flag & _IOSTRG)) {
        return SpinlockAcquire(&get_ioinfo(file->_fd)->lock);
    }
    return OsSuccess;
}

/* _unlock_file
 * Performs primitive unlocking on a file-stream. */
OsStatus_t
_unlock_file(
    _In_ FILE *file)
{
    if (!(file->_flag & _IOSTRG)) {
        return SpinlockRelease(&get_ioinfo(file->_fd)->lock);
    }
    return OsSuccess;
}

#endif
