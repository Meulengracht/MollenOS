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

#include <os/driver/input.h>
#include <os/driver/file.h>
#include <os/ipc/ipc.h>
#include <os/thread.h>
#include <ds/list.h>
#include <stdio.h>
#include <errno.h>
#include <io.h>
#include "local.h"

/* Globals
 * Used to keep state of all io-objects */
Spinlock_t __GlbFdBitmapLock;
List_t *__GlbIoObjects = NULL;
int *__GlbFdBitmap = NULL;
FILE __GlbStdout, __GlbStdin, __GlbStderr;

/* StdioInitialize
 * Initializes default handles and resources */
void
StdioInitialize(void)
{
    // Initialize the list of io-objects
    __GlbIoObjects = ListCreate(KeyInteger, LIST_SAFE);

    // Initialize the bitmap of fds
    __GlbFdBitmap = (int *)malloc(DIVUP(INTERNAL_MAXFILES, 8));
    memset(__GlbFdBitmap, 0, DIVUP(INTERNAL_MAXFILES, 8));
    SpinlockReset(&__GlbFdBitmapLock);

    // Allocate the initial 3 fds
    __GlbFdBitmap[0] |= (0x1 | 0x2 | 0x4);

    // Initialize the STDOUT handle

    // Initialize the STDIN handle

    // Initialize the STDERR handle
}

/* StdioCleanup
 * */
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
StdioFdValid(int fd)
{
    return fd >= 0 && fd < INTERNAL_MAXFILES 
        && (get_ioinfo(fd)->wxflag & WX_OPEN);
}

/* StdioFdAllocate 
 * Allocates a new file descriptor handle from the bitmap.
 * Returns -1 on error. */
int
StdioFdAllocate(UUId_t handle, int flag)
{
    // Variables
    ioobject *io = NULL;
    DataKey_t Key;
    int i, j, result = -1;

    // Acquire the bitmap lock
    SpinlockAcquire(&__GlbFdBitmapLock);

    // Iterate the bitmap and find a free fd
    for (i = 0; i < DIVUP(INTERNAL_MAXFILES, (8 * sizeof(int))); i++)
    {
        for (j = 0; i < (8 * sizeof(int)); j++)
        {
            if (!(__GlbFdBitmap[i] & (1 << j)))
            {
                __GlbFdBitmap[i] |= (1 << j);
                result = (i * (8 * sizeof(int))) + j;
                break;
            }
        }

        // Early breakout?
        if (j != (8 * sizeof(int)))
        {
            break;
        }
    }

    // Release lock before returning
    SpinlockRelease(&__GlbFdBitmapLock);

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
        ListAppend(__GlbIoObjects, ListCreateNode(Key, Key, io));
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
    ListNode_t *fNode;
    DataKey_t Key;

    // Free any resources allocated by the fd
    Key.Value = fd;
    fNode = ListGetNodeByKey(__GlbIoObjects, Key, 0);
    if (fNode != NULL) {
        free(fNode->Data);
        ListRemoveByNode(__GlbIoObjects, fNode);
        ListDestroyNode(__GlbIoObjects, fNode);
    }

    // Acquire the bitmap lock
    SpinlockAcquire(&__GlbFdBitmapLock);

    // Set the given fd index to free
    __GlbFdBitmap[fd / (8 * sizeof(int))] &=
        ~(1 << (fd % (8 * sizeof(int))));

    // Release lock before returning
    SpinlockRelease(&__GlbFdBitmapLock);
}

/* StdioFdToHandle
 * Retrieves the file descriptor os-handle from the given fd */
UUId_t
StdioFdToHandle(
    _In_ int fd)
{
    // Variables
    ListNode_t *fNode;
    DataKey_t Key;

    // Free any resources allocated by the fd
    Key.Value = fd;
    fNode = ListGetNodeByKey(__GlbIoObjects, Key, 0);
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
    ListNode_t *fNode;
    DataKey_t Key;

    // Lookup node of the file descriptor
    Key.Value = fd;
    fNode = ListGetNodeByKey(__GlbIoObjects, Key, 0);
    
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
 * */
int
StdioReadStdin(void)
{
	// Variables
	MRemoteCall_t Event;
	MInput_t *Input = NULL;
	int Character = 0;
	int Run = 1;

	// Wait for input message, we need to discard 
	// everything else as this is a polling op
	while (Run) {
		if (RPCListen(&Event) == OsSuccess) {
			Input = (MInput_t*)Event.Arguments[0].Data.Buffer;
			if (Event.Function == EVENT_INPUT) {
				if (Input->Type == InputKeyboard
					&& (Input->Flags & INPUT_BUTTON_CLICKED)) {
					Character = (int)Input->Key;
					Run = 0;
				}
			}
		}
		RPCCleanup(&Event);
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
	size_t OriginalSize = GetBufferSize(TLSGetCurrent()->Transfer);
	uint8_t *Pointer = (uint8_t*)Buffer;
    UUId_t Handle = StdioFdToHandle(fd);
    
    // Determine handle
    if (Handle == UUID_INVALID) {
        // Only one case this is allowed
        if (__GlbStdin._fd == fd) {
            switch (BytesLeft) {
                case 1: {
                    *Pointer = (uint8_t)StdioReadStdin();
                } break;
                case 2: {
                    *((uint16_t*)Pointer) = (uint16_t)StdioReadStdin();
                } break;
                case 4: {
                    *((uint32_t*)Pointer) = (uint32_t)StdioReadStdin();
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
		size_t BytesRead = 0, BytesIndex = 0;
		ChangeBufferSize(TLSGetCurrent()->Transfer, ChunkSize);
        if (_fval(ReadFile(Handle, TLSGetCurrent()->Transfer, 
            &BytesIndex, &BytesRead))) {
			break;
		}
		if (BytesRead == 0) {
			break;
		}
		SeekBuffer(TLSGetCurrent()->Transfer, BytesIndex);
        ReadBuffer(TLSGetCurrent()->Transfer, 
            (__CONST void*)Pointer, BytesRead, NULL);
		SeekBuffer(TLSGetCurrent()->Transfer, 0);
		BytesReadTotal += BytesRead;
		BytesLeft -= BytesRead;
		Pointer += BytesRead;
	}

	// Restore transfer buffer
	return ChangeBufferSize(
        TLSGetCurrent()->Transfer, OriginalSize);
}

/* getstdfile
 * Retrieves a standard io stream handle */
FILE *
getstdfile(
    _In_ int n)
{
    switch (n)
    {
    case STDOUT_FD:
    {
        return &__GlbStdout;
    }
    case STDIN_FD:
    {
        return &__GlbStdin;
    }
    case STDERR_FD:
    {
        return &__GlbStderr;
    }
    default:
    {
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
        if (cnt > 0 && _write(file->_fd, file->_base, cnt) != cnt)
        {
            file->_flag |= _IOERR;
            return OsError;
        }

        // If it's rw, clear the write flag
        if (file->_flag & _IORW)
        {
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
    foreach(fNode, __GlbIoObjects) {
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

int
_fcloseall(void)
{
    int num_closed = 0;
    FILE *file;

    LOCK_FILES();
    foreach(fNode, __GlbIoObjects) {
        file = (FILE*)(((ioobject*)fNode->Data)->file);

        // Close found file
        if (!fclose(file)) {
            num_closed++;      
        }
    }
    UNLOCK_FILES();

    return num_closed;
}

int
_isatty(
    _In_ int fd)
{
    return get_ioinfo(fd)->wxflag & WX_TTY;
}

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
    if ((file->_fd == STDOUT_FD || file->_fd == STDERR_FD) && _isatty(file->_fd))
    {
        return OsError;
    }

    // Allocate a transfer buffer
    file->_base = calloc(1, INTERNAL_BUFSIZ);
    if (file->_base)
    {
        file->_bufsiz = INTERNAL_BUFSIZ;
        file->_flag |= _IOMYBUF;
    }
    else
    {
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
    if ((file->_fd != STDOUT_FD && file->_fd != STDERR_FD) || (file->_flag & (_IONBF | _IOMYBUF | _USERBUF)) || !_isatty(file->_fd))
    {
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
 * */
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
 * */
OsStatus_t
_unlock_file(
    _In_ FILE *file)
{
    if (!(file->_flag & _IOSTRG)) {
        return SpinlockRelease(&get_ioinfo(file->_fd)->lock);
    }
    return OsSuccess;
}
