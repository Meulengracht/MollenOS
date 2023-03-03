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

//#define __TRACE
#define __need_minmax
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_tls.h>
#include <io.h>
#include <os/services/file.h>
#include <os/shm.h>

static oserr_t __file_read(stdio_handle_t*, void*, size_t, size_t*);
static oserr_t __file_write(stdio_handle_t*, const void*, size_t, size_t*);
static oserr_t __file_resize(stdio_handle_t*, long long);
static oserr_t __file_seek(stdio_handle_t*, int, off64_t, long long*);
static oserr_t __file_ioctl(stdio_handle_t*, int, va_list);
static void    __file_close(stdio_handle_t*, int);

#define BOM_MAX_LEN 4

static const struct bom_mode {
    const char*   name;
    long          len;
    unsigned int  flags;
    unsigned char identifier[BOM_MAX_LEN];
} supported_bom_modes[] = {
        { "UTF32BE", 4, WX_UTF32 | WX_BIGENDIAN, { 0x00, 0x00, 0xFE, 0xFF } },
        { "UTF32LE", 4, WX_UTF32, { 0xFF, 0xFE, 0x00, 0x00 } },
        { "UTF16BE", 2, WX_UTF16 | WX_BIGENDIAN, { 0xFE, 0xFF } },
        { "UTF16LE", 2, WX_UTF16, { 0xFF, 0xFE } },
        { "UTF8", 3, WX_UTF, { 0xEF, 0xBB, 0xBF } },
        { NULL, 0, 0, { 0 }}
};
static stdio_ops_t g_fileOps = {
        .read = __file_read,
        .write = __file_write,
        .resize = __file_resize,
        .seek = __file_seek,
        .ioctl = __file_ioctl,
        .close = __file_close
};

static unsigned int __detect_filemode(int iod)
{
    char bomBuffer[BOM_MAX_LEN];
    int  bytesRead;

    bytesRead = read(iod, &bomBuffer[0], sizeof(bomBuffer));
    if (bytesRead > 0) {
        int i = 0;

        while (supported_bom_modes[i].name != NULL) {
            if (bytesRead >= supported_bom_modes[i].len &&
                !memcmp(&bomBuffer[0], &supported_bom_modes[i].identifier[0], supported_bom_modes[i].len)) {
                TRACE(STR("[__detect_filemode] mode automatically set to %s"), supported_bom_modes[i].name);
                lseek(iod, supported_bom_modes[i].len, SEEK_SET);
                return supported_bom_modes[i].flags;
            }
            i++;
        }
    }

    // not able to detect mode
    lseek(iod, 0, SEEK_SET);
    return 0;
}

// return -1 on fail and set errno
int open(const char* file, int flags, ...)
{
    int             status;
    oserr_t         osStatus;
    stdio_handle_t* object;
    OSHandle_t      handle;
    unsigned int    mode = 0;
    va_list         ap;

    if (!file) {
        _set_errno(EINVAL);
        return -1;
    }

    // Extract mode flags
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_start(ap, flags);
        mode = va_arg(ap, unsigned int);
        va_end(ap);
    }

    // Try to open the file by directly communicating with the file-service
    osStatus = OSOpenPath(
            file,
            _fopts(flags) | _faccess(flags),
            _fperms(mode),
            &handle
    );
    if (osStatus != OS_EOK) {
        return OsErrToErrNo(osStatus);
    }

    status = stdio_handle_create2(
            -1,
            flags,
            0,
            FILE_SIGNATURE,
            &g_fileOps,
            NULL,
            &object
    );
    if (status) {
        (void)OSCloseFile(&handle);
        return status;
    }
    stdio_handle_set_handle(object, &handle);

    // detect filemode automatically
    if (flags & O_TEXT) {
        unsigned int detectedMode;

        object->wxflag &= ~WX_TEXT;
        detectedMode = __detect_filemode(object->fd);
        object->wxflag |= WX_TEXT;
        if (detectedMode) {
            object->wxflag &= ~WX_TEXT_FLAGS;
            object->wxflag |= detectedMode;
        }
    }
    return object->fd;
}

oserr_t
stdio_file_op_read(
        _In_  stdio_handle_t* handle,
        _In_  void*           buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytesReadOut);

oserr_t
stdio_file_op_write(
        _In_  stdio_handle_t* handle,
        _In_  const void*     buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytesWrittenOut);

static inline oserr_t
__transfer(
        _In_  uuid_t  fileID,
        _In_  uuid_t  bufferID,
        _In_  bool    write,
        _In_  size_t  chunkSize,
        _In_  size_t  offset,
        _In_  size_t  length,
        _Out_ size_t* bytesTransferreOut)
{
    size_t  bytesLeft = length;
    oserr_t oserr;
    TRACE("[libc] [file-io] [perform_transfer] length %" PRIuIN, length);

    // Keep reading chunks untill we've read all requested
    while (bytesLeft > 0) {
        size_t bytesToTransfer = MIN(chunkSize, bytesLeft);
        size_t bytesTransferred;

        TRACE("[libc] [file-io] [perform_transfer] chunk size %" PRIuIN ", offset %" PRIuIN,
              bytesToTransfer, offset);
        oserr = OSTransferFile(fileID, bufferID, offset, write, bytesToTransfer, &bytesTransferred)
                TRACE("[libc] [file-io] [perform_transfer] bytes read %" PRIuIN ", status %u",
                      bytesTransferred, oserr);
        if (oserr != OS_EOK || bytesTransferred == 0) {
            break;
        }

        bytesLeft -= bytesTransferred;
        offset    += bytesTransferred;
    }

    *bytesTransferreOut = length - bytesLeft;
    return oserr;
}

static oserr_t
__read_large(
        _In_  stdio_handle_t* handle,
        _In_  void*           buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytesReadOut)
{
    SHMHandle_t shm;
    void*       adjustedPointer = (void*)buffer;
    size_t      adjustedLength  = length;
    oserr_t     oserr;

    // enforce dword alignment on the buffer
    // which means if someone passes us a byte or word aligned
    // buffer we must account for that
    if ((uintptr_t)buffer & 0x3) {
        size_t bytesToAlign = 4 - ((uintptr_t)buffer & 0x3);
        oserr = stdio_file_op_read(handle, buffer, bytesToAlign, bytesReadOut);
        if (oserr != OS_EOK) {
            return oserr;
        }
        adjustedPointer = (void*)((uintptr_t)buffer + bytesToAlign);
        adjustedLength -= bytesToAlign;
    }

    oserr = SHMExport(
            adjustedPointer,
            &(SHM_t) {
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = adjustedLength
            },
            &shm
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Pass the callers pointer directly here
    oserr = __transfer(
            handle->handle.ID,
            shm.ID,
            false,
            adjustedLength,
            0,
            adjustedLength,
            bytesReadOut
    );
    if (*bytesReadOut == adjustedLength) {
        *bytesReadOut = length;
    }

    SHMDetach(&shm);
    return oserr;
}

static oserr_t
__file_read(
        _In_  stdio_handle_t* handle,
        _In_  void*           buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytesReadOut)
{
    SHMHandle_t* shmHandle = __tls_current_dmabuf();
    uuid_t       builtinHandle = shmHandle->ID;
    size_t       builtinLength = shmHandle->Length;
    size_t       bytesRead;
    oserr_t      oserr;
    TRACE("stdio_file_op_read(buffer=0x%" PRIxIN ", length=%" PRIuIN ")", buffer, length);

    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once.
    if (length > builtinLength) {
        return __read_large(handle, buffer, length, bytesReadOut);
    }

    oserr = __transfer(handle->handle.ID, builtinHandle, 0,
                       builtinLength, 0, length, &bytesRead);
    if (oserr == OS_EOK && bytesRead > 0) {
        memcpy(buffer, shmHandle->Buffer, bytesRead);
    }

    *bytesReadOut = bytesRead;
    return oserr;
}

static oserr_t
__write_large(
        _In_  stdio_handle_t* handle,
        _In_  const void*     buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytesWrittenOut)
{
    SHMHandle_t shm;
    void*       adjustedPointer = (void*)buffer;
    size_t      adjustedLength  = length;
    oserr_t     oserr;

    // enforce dword alignment on the buffer
    // which means if someone passes us a byte or word aligned
    // buffer we must account for that
    if ((uintptr_t)buffer & 0x3) {
        size_t bytesToAlign = 4 - ((uintptr_t)buffer & 0x3);
        oserr = stdio_file_op_write(handle, buffer, bytesToAlign, bytesWrittenOut);
        if (oserr != OS_EOK) {
            return oserr;
        }
        adjustedPointer = (void*)((uintptr_t)buffer + bytesToAlign);
        adjustedLength -= bytesToAlign;
    }

    oserr = SHMExport(
            adjustedPointer,
            &(SHM_t) {
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
                    .Size = adjustedLength
            },
            &shm
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __transfer(handle->handle.ID, shm.ID,
                       1, adjustedLength, 0, adjustedLength, bytesWrittenOut);
    SHMDetach(&shm);
    if (*bytesWrittenOut == adjustedLength) {
        *bytesWrittenOut = length;
    }
    return oserr;
}

static oserr_t
__file_write(
        _In_  stdio_handle_t* handle,
        _In_  const void*     buffer,
        _In_  size_t          length,
        _Out_ size_t*         bytesWrittenOut)
{
    SHMHandle_t* shmHandle = __tls_current_dmabuf();
    uuid_t       builtinHandle = shmHandle->ID;
    size_t       builtinLength = shmHandle->Length;
    oserr_t      oserr;
    TRACE("stdio_file_op_write(buffer=0x%" PRIxIN ", length=%" PRIuIN ")", buffer, length);

    // There is a time when reading more than a couple of times is considerably slower
    // than just reading the entire thing at once.
    if (length > builtinLength) {
        return __write_large(handle, buffer, length, bytesWrittenOut);
    }

    memcpy(shmHandle->Buffer, buffer, length);
    oserr = __transfer(handle->handle.ID, builtinHandle, 1,
                       builtinLength, 0, length, bytesWrittenOut);
    return oserr;
}

static oserr_t
__file_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* position_out)
{
    oserr_t      status;
    UInteger64_t seekFinal;
    TRACE("stdio_file_op_seek(origin=%i, offset=%" PRIiIN ")", origin, offset);

    // If we search from SEEK_SET, just build offset directly
    if (origin != SEEK_SET) {
        UInteger64_t currentOffset;

        // Adjust for seek origin
        if (origin == SEEK_CUR) {
            status = OSGetFilePosition(handle->handle.ID, &currentOffset);
            if (status != OS_EOK) {
                ERROR("failed to get file position");
                return status;
            }

            // Sanitize for overflow
            if ((size_t)currentOffset.QuadPart != currentOffset.QuadPart) {
                ERROR("file-offset-overflow");
                _set_errno(EOVERFLOW);
                return OS_EUNKNOWN;
            }
        } else {
            status = OSGetFileSize(handle->handle.ID, &currentOffset);
            if (status != OS_EOK) {
                ERROR("failed to get file size");
                return status;
            }
        }
        seekFinal.QuadPart = currentOffset.QuadPart + offset;
    } else {
        seekFinal.QuadPart = offset;
    }

    // no reason to invoke the service
    if (origin == SEEK_CUR && offset == 0) {
        *position_out = (long long int)seekFinal.QuadPart;
        return OS_EOK;
    }

    // Now perform the seek
    status = OSSeekFile(handle->handle.ID, &seekFinal);
    if (status == OS_EOK) {
        *position_out = (long long int)seekFinal.QuadPart;
        return OS_EOK;
    }
    TRACE("stdio::fseek::fail %u", status);
    *position_out = (off_t)-1;
    return status;
}

static oserr_t
__file_resize(stdio_handle_t* handle, long long resize_by)
{
    // TODO: OSFileTruncate()
    return OS_ENOTSUPPORTED;
}

static void
__file_close(stdio_handle_t* handle, int options)
{
    if (options & STDIO_CLOSE_FULL) {
        (void)OSCloseFile(&handle->handle);
    }
}

static oserr_t
__file_ioctl(stdio_handle_t* handle, int request, va_list vlist)
{
    return OS_ENOTSUPPORTED;
}
