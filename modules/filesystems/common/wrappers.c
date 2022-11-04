/**
 * Copyright 2022, Philip Meulengracht
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

#include <ioset.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <fs/requests.h>
#include <os/usched/job.h>
#include <os/dmabuf.h>

#include <ctt_filesystem_service_server.h>

extern oserr_t
FsInitialize(
        _In_  struct VFSStorageParameters* storageParameters,
        _Out_ void**                       instanceData);

void FsInitializeWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    void*   fscontext;
    oserr_t oserr;

    oserr = FsInitialize(&request->parameters.init, &fscontext);
    ctt_filesystem_setup_response(request->message, oserr, (uintptr_t)fscontext);
    FSRequestDestroy(request);
}

extern oserr_t
FsDestroy(
        _In_ void*         instanceData,
        _In_ unsigned int  unmountFlags);

void FsDestroyWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    oserr_t oserr;

    oserr = FsDestroy(request->parameters.destroy.context, 0);
    ctt_filesystem_destroy_response(request->message, oserr);
    FSRequestDestroy(request);
}

extern oserr_t
FsOpen(
        _In_      void*      instanceData,
        _In_      mstring_t* path,
        _Out_Opt_ void**     dataOut);

void FsOpenWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    void*   fcontext;
    oserr_t oserr;

    oserr = FsOpen(
            request->parameters.open.context,
            request->parameters.open.path,
            &fcontext
    );
    ctt_filesystem_open_response(request->message, oserr, (uintptr_t)fcontext);
    mstr_delete(request->parameters.open.path);
    FSRequestDestroy(request);
}

extern oserr_t
FsCreate(
        _In_  void*      instanceData,
        _In_  void*      data,
        _In_  mstring_t* name,
        _In_  uint32_t   owner,
        _In_  uint32_t   flags,
        _In_  uint32_t   permissions,
        _Out_ void**     dataOut);

void FsCreateWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    void*   fcontext;
    oserr_t oserr;

    oserr = FsCreate(
            request->parameters.create.fscontext,
            request->parameters.create.fcontext,
            request->parameters.create.name,
            request->parameters.create.owner,
            request->parameters.create.flags,
            request->parameters.create.permissions,
            &fcontext
    );
    ctt_filesystem_create_response(request->message, oserr, (uintptr_t)fcontext);
    mstr_delete(request->parameters.create.name);
    FSRequestDestroy(request);
}

extern
oserr_t
FsClose(
        _In_ void* instanceData,
        _In_ void* data);

void FsCloseWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    oserr_t oserr;

    oserr = FsClose(
            request->parameters.close.fscontext,
            request->parameters.close.fcontext
    );
    ctt_filesystem_close_response(request->message, oserr);
    FSRequestDestroy(request);
}

extern oserr_t
FsStat(
        _In_ void*             instanceData,
        _In_ struct VFSStatFS* stat);

void FsStatWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    struct VFSStatFS  stats;
    struct ctt_fsstat response;
    oserr_t           oserr;

    oserr = FsStat(
            request->parameters.stat.fscontext,
            &stats
    );
    to_fsstat(&stats, &response);
    ctt_filesystem_fsstat_response(request->message, oserr, &response);
    FSRequestDestroy(request);
}

extern oserr_t
FsLink(
        _In_ void*      instanceData,
        _In_ void*      data,
        _In_ mstring_t* linkName,
        _In_ mstring_t* linkTarget,
        _In_ int        symbolic);

void FsLinkWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    oserr_t oserr;

    oserr = FsLink(
            request->parameters.link.fscontext,
            request->parameters.link.fcontext,
            request->parameters.link.name,
            request->parameters.link.target,
            request->parameters.link.symbolic
    );
    ctt_filesystem_link_response(request->message, oserr);
    mstr_delete(request->parameters.link.name);
    mstr_delete(request->parameters.link.target);
    FSRequestDestroy(request);
}

extern oserr_t
FsUnlink(
        _In_  void*     instanceData,
        _In_ mstring_t* path);

void FsUnlinkWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    oserr_t oserr;

    oserr = FsUnlink(
            request->parameters.unlink.fscontext,
            request->parameters.unlink.path
    );
    ctt_filesystem_unlink_response(request->message, oserr);
    mstr_delete(request->parameters.unlink.path);
    FSRequestDestroy(request);
}

extern oserr_t
FsReadLink(
        _In_ void*       instanceData,
        _In_ mstring_t*  path,
        _In_ mstring_t** pathOut);

void FsReadLinkWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    mstring_t* path;
    oserr_t    oserr;

    oserr = FsReadLink(
            request->parameters.readlink.fscontext,
            request->parameters.readlink.path,
            &path
    );
    ctt_filesystem_readlink_response(request->message, oserr, mstr_u8(path));
    mstr_delete(request->parameters.readlink.path);
    FSRequestDestroy(request);
}

extern oserr_t
FsMove(
        _In_ void*      instanceData,
        _In_ mstring_t* from,
        _In_ mstring_t* to,
        _In_ int        copy);

void FsMoveWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    mstring_t* path;
    oserr_t    oserr;

    oserr = FsMove(
            request->parameters.move.fscontext,
            request->parameters.move.from,
            request->parameters.move.to,
            request->parameters.move.copy
    );
    ctt_filesystem_move_response(request->message, oserr);
    mstr_delete(request->parameters.move.from);
    mstr_delete(request->parameters.move.to);
    FSRequestDestroy(request);
}

extern oserr_t
FsRead(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsRead);

static oserr_t
__MapUserBufferRead(
        _In_ uuid_t           handle,
        _In_ DMAAttachment_t* attachment)
{
    oserr_t osStatus;

    osStatus = DmaAttach(handle, attachment);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // When mapping the buffer for reading, we need write access to the buffer,
    // so we can do buffer combining.
    osStatus = DmaAttachmentMap(attachment, DMA_ACCESS_WRITE);
    if (osStatus != OS_EOK) {
        DmaDetach(attachment);
        return osStatus;
    }
    return OS_EOK;
}

void FsReadWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    oserr_t         oserr;
    DMAAttachment_t attachment;
    size_t          read;

    oserr = __MapUserBufferRead(request->parameters.transfer.buffer_id, &attachment);
    if (oserr != OS_EOK) {
        ctt_filesystem_read_response(request->message, oserr, 0);
    }

    oserr = FsRead(
            request->parameters.transfer.fscontext,
            request->parameters.transfer.fcontext,
            request->parameters.transfer.buffer_id,
            attachment.buffer,
            request->parameters.transfer.offset,
            (size_t)request->parameters.transfer.count, // TODO: be consistent with size units
            &read
    );
    ctt_filesystem_read_response(request->message, oserr, read);

    oserr = DmaDetach(&attachment);
    if (oserr != OS_EOK) {
        WARNING("FsReadWrapper failed to detach read buffer");
    }
    FSRequestDestroy(request);
}

extern oserr_t
FsWrite(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsWritten);

static oserr_t
__MapUserBufferWrite(
        _In_ uuid_t           handle,
        _In_ DMAAttachment_t* attachment)
{
    oserr_t oserr;

    oserr = DmaAttach(handle, attachment);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = DmaAttachmentMap(attachment, 0);
    if (oserr != OS_EOK) {
        DmaDetach(attachment);
        return oserr;
    }
    return OS_EOK;
}

void FsWriteWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    oserr_t         oserr;
    DMAAttachment_t attachment;
    size_t          written;

    oserr = __MapUserBufferWrite(request->parameters.transfer.buffer_id, &attachment);
    if (oserr != OS_EOK) {
        ctt_filesystem_write_response(request->message, oserr, 0);
    }

    oserr = FsWrite(
            request->parameters.transfer.fscontext,
            request->parameters.transfer.fcontext,
            request->parameters.transfer.buffer_id,
            attachment.buffer,
            request->parameters.transfer.offset,
            (size_t)request->parameters.transfer.count, // TODO: be consistent with size units
            &written
    );
    ctt_filesystem_write_response(request->message, oserr, written);

    oserr = DmaDetach(&attachment);
    if (oserr != OS_EOK) {
        WARNING("FsWriteWrapper failed to detach read buffer");
    }
    FSRequestDestroy(request);
}

extern oserr_t
FsTruncate(
        _In_ void*    instanceData,
        _In_ void*    data,
        _In_ uint64_t size);

void FsTruncateWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    oserr_t oserr;

    oserr = FsTruncate(
            request->parameters.truncate.fscontext,
            request->parameters.truncate.fcontext,
            request->parameters.truncate.size
    );
    ctt_filesystem_truncate_response(request->message, oserr);
    FSRequestDestroy(request);
}

extern oserr_t
FsSeek(
        _In_  void*     instanceData,
        _In_  void*     data,
        _In_  uint64_t  absolutePosition,
        _Out_ uint64_t* absolutePositionOut);

void FsSeekWrapper(
        _In_ FileSystemRequest_t* request,
        _In_ void*                cancellationToken)
{
    uint64_t position;
    oserr_t  oserr;

    oserr = FsSeek(
            request->parameters.seek.fscontext,
            request->parameters.seek.fcontext,
            request->parameters.seek.position,
            &position
    );
    ctt_filesystem_seek_response(request->message, oserr, position);
    FSRequestDestroy(request);
}
