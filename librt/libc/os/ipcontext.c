/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * Ip-Context Support Definitions & Structures
 * - This header describes the base ipc-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

//#define __TRACE

#include <ddk/handle.h>
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_ipc.h>
#include <internal/_io.h>
#include <internal/_syscalls.h>
#include <ipcontext.h>
#include <os/mollenos.h>

int ipcontext(unsigned int len, struct ipmsg_addr* addr)
{
    stdio_handle_t* io_object;
    int             status;
    OsStatus_t      os_status;
    streambuffer_t* stream;
    UUId_t          handle;

    TRACE("ipcontext(len=%u, addr=0x" PRIxIN ")", len, addr);
    
    if (!len) {
        _set_errno(EINVAL);
        return -1;
    }
    
    os_status = Syscall_IpcContextCreate(len, &handle, &stream);
    if (os_status != OsSuccess) {
        OsStatusToErrno(os_status);
        return -1;
    }
    
    if (addr && addr->type == IPMSG_ADDRESS_PATH) {
        os_status = handle_set_path(handle, addr->data.path);
        if (os_status != OsSuccess) {
            handle_destroy(handle);
            OsStatusToErrno(os_status);
            return -1;
        }
    }
    
    status = stdio_handle_create(-1, WX_OPEN | WX_PIPE | WX_DONTINHERIT, &io_object);
    if (status) {
        handle_destroy(handle);
        return -1;
    }
    
    stdio_handle_set_handle(io_object, handle);
    stdio_handle_set_ops_type(io_object, STDIO_HANDLE_IPCONTEXT);
    
    io_object->object.data.ipcontext.stream  = stream;
    io_object->object.data.ipcontext.options = 0;
    
    return io_object->fd;
}

int putmsg(int iod, struct ipmsg_header* msg, int timeout)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    OsStatus_t      status;
    
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (!msg) {
        _set_errno(EINVAL);
        return -1;
    }
    
    if (handle->object.type != STDIO_HANDLE_IPCONTEXT) {
        _set_errno(EINVAL);
        return -1;
    }
    
    status = Syscall_IpcContextSend(&msg, 1, timeout);
    return OsStatusToErrno(status);
}

int getmsg(int iod, struct ipmsg* msg, unsigned int len, int flags)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    size_t          bytesAvailable;
    unsigned int    base;
    unsigned int    state;
    streambuffer_t* stream;
    unsigned int    sb_options = 0;
    int             status = 0;

    TRACE("getmsg(iod=%i, msg=0x%" PRIxIN ", len=%u, flags=0x%x", iod, msg, len, flags);
    
    if (!handle) {
        _set_errno(EBADF);
        status = -1;
        goto exit;
    }
    
    if (!len || !msg) {
        _set_errno(EINVAL);
        status = -1;
        goto exit;
    }
    
    if (handle->object.type != STDIO_HANDLE_IPCONTEXT) {
        _set_errno(EINVAL);
        status = -1;
        goto exit;
    }
    
    if (flags & IPMSG_DONTWAIT) {
        sb_options |= STREAMBUFFER_NO_BLOCK;
    }
    
    stream         = handle->object.data.ipcontext.stream;
    bytesAvailable = streambuffer_read_packet_start(stream, sb_options, &base, &state);
    if (!bytesAvailable) {
        _set_errno(ENODATA);
        status = -1;
        goto exit;
    }
    
    TRACE("getmsg message, size=%" PRIuIN, bytesAvailable);
    streambuffer_read_packet_data(stream, msg, MIN(len, bytesAvailable), &state);
    streambuffer_read_packet_end(stream, base, bytesAvailable);

exit:
    TRACE("getmsg return=%i", status);
    return status;
}

int resp(int iod, struct ipmsg* msg, struct ipmsg_header* resp)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    OsStatus_t      status;
    
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (!msg || !resp) {
        _set_errno(EINVAL);
        return -1;
    }
    
    if (handle->object.type != STDIO_HANDLE_IPCONTEXT) {
        _set_errno(EINVAL);
        return -1;
    }

    status = Syscall_IpcContextRespond(&msg, &resp, 1);
    return OsStatusToErrno(status);
}
