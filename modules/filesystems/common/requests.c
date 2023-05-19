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

#define __TRACE

#include <ioset.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <fs/common.h>
#include <os/handle.h>
#include <os/shm.h>
#include <os/usched/job.h>

#include <ctt_filesystem_service_server.h>

extern oserr_t
FsInitialize(
        _In_  struct VFSStorageParameters* storageParameters,
        _Out_ void**                       instanceData);

void ctt_filesystem_setup_invocation(struct gracht_message* message, const struct ctt_fs_setup_params* params)
{
    struct VFSStorageParameters vfsParams;
    void*   fscontext;
    oserr_t oserr;
    TRACE("ctt_filesystem_setup_invocation()");

    from_fs_setup_params(params, &vfsParams);
    oserr = FsInitialize(&vfsParams, &fscontext);
    ctt_filesystem_setup_response(message, oserr, (uintptr_t)fscontext);
}

extern oserr_t
FsDestroy(
        _In_ void*         instanceData,
        _In_ unsigned int  unmountFlags);

void ctt_filesystem_destroy_invocation(struct gracht_message* message, const uintptr_t fsctx)
{
    oserr_t oserr;
    TRACE("ctt_filesystem_destroy_invocation()");

    oserr = FsDestroy((void*)fsctx, 0);
    ctt_filesystem_destroy_response(message, oserr);
}

extern oserr_t
FsStat(
        _In_ void*             instanceData,
        _In_ struct VFSStatFS* stat);

void ctt_filesystem_fsstat_invocation(struct gracht_message* message, const uintptr_t fsctx)
{
    struct VFSStatFS  stats;
    struct ctt_fsstat response;
    oserr_t           oserr;
    TRACE("ctt_filesystem_fsstat_invocation()");

    oserr = FsStat((void*)fsctx, &stats);
    to_fsstat(&stats, &response);
    ctt_filesystem_fsstat_response(message, oserr, &response);
    free(response.label);
}

extern oserr_t
FsOpen(
        _In_      void*      instanceData,
        _In_      mstring_t* path,
        _Out_Opt_ void**     dataOut);

void ctt_filesystem_open_invocation(struct gracht_message* message, const uintptr_t fsctx, const char* path)
{
    void*      fcontext;
    mstring_t* mpath;
    oserr_t    oserr;
    TRACE("ctt_filesystem_open_invocation()");

    mpath = mstr_new_u8(path);
    if (mpath == NULL) {
        ctt_filesystem_open_response(message, OS_EOOM, 0);
        return;
    }

    oserr = FsOpen((void*)fsctx, mpath, &fcontext);
    ctt_filesystem_open_response(message, oserr, (uintptr_t)fcontext);
    mstr_delete(mpath);
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

void ctt_filesystem_create_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx, const struct ctt_fs_open_params* params)
{
    void*      fcontext;
    mstring_t* mname;
    oserr_t    oserr;
    TRACE("ctt_filesystem_create_invocation()");

    mname = mstr_new_u8(params->name);
    if (mname == NULL) {
        ctt_filesystem_create_response(message, OS_EOOM, 0);
        return;
    }

    oserr = FsCreate(
            (void*)fsctx,
            (void*)fctx,
            mname,
            params->owner,
            params->flags,
            params->permissions,
            &fcontext
    );
    ctt_filesystem_create_response(message, oserr, (uintptr_t)fcontext);
    mstr_delete(mname);
}

extern
oserr_t
FsClose(
        _In_ void* instanceData,
        _In_ void* data);

void ctt_filesystem_close_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx)
{
    oserr_t oserr;
    TRACE("ctt_filesystem_close_invocation()");

    oserr = FsClose((void*)fsctx, (void*)fctx);
    ctt_filesystem_close_response(message, oserr);
}

extern oserr_t
FsLink(
        _In_ void*      instanceData,
        _In_ void*      data,
        _In_ mstring_t* linkName,
        _In_ mstring_t* linkTarget,
        _In_ int        symbolic);

void ctt_filesystem_link_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx,
        const char* name, const char* target, const uint8_t symbolic)
{
    mstring_t* mname;
    mstring_t* mtarget;
    oserr_t    oserr;
    TRACE("ctt_filesystem_link_invocation()");

    mname = mstr_new_u8(name);
    if (mname == NULL) {
        ctt_filesystem_link_response(message, OS_EOOM);
        return;
    }

    mtarget = mstr_new_u8(target);
    if (mtarget == NULL) {
        mstr_delete(mname);
        ctt_filesystem_link_response(message, OS_EOOM);
        return;
    }

    oserr = FsLink(
            (void*)fsctx,
            (void*)fctx,
            mname,
            mtarget,
            symbolic
    );
    ctt_filesystem_link_response(message, oserr);
    mstr_delete(mname); mstr_delete(mtarget);
}

extern oserr_t
FsUnlink(
        _In_  void*     instanceData,
        _In_ mstring_t* path);

void ctt_filesystem_unlink_invocation(struct gracht_message* message, const uintptr_t fsctx, const char* path)
{
    mstring_t* mpath;
    oserr_t    oserr;
    TRACE("ctt_filesystem_unlink_invocation()");

    mpath = mstr_new_u8(path);
    if (mpath == NULL) {
        ctt_filesystem_unlink_response(message, OS_EOOM);
        return;
    }

    oserr = FsUnlink((void*)fsctx, mpath);
    ctt_filesystem_unlink_response(message, oserr);
    mstr_delete(mpath);
}

extern oserr_t
FsReadLink(
        _In_ void*       instanceData,
        _In_ mstring_t*  path,
        _In_ mstring_t** pathOut);

void ctt_filesystem_readlink_invocation(struct gracht_message* message, const uintptr_t fsctx, const char* path)
{
    mstring_t* mpath;
    mstring_t* resultPath = NULL;
    oserr_t    oserr;
    TRACE("ctt_filesystem_readlink_invocation()");

    mpath = mstr_new_u8(path);
    if (mpath == NULL) {
        ctt_filesystem_readlink_response(message, OS_EOOM, "");
        return;
    }

    oserr = FsReadLink((void*)fsctx, mpath,  &resultPath);
    ctt_filesystem_readlink_response(message, oserr, mstr_u8(resultPath));
    mstr_delete(mpath);
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

static inline oserr_t
__MapUserBufferRead(
        _In_ struct FSBaseContext* fsBaseContext,
        _In_ uuid_t                handle,
        _In_ size_t                offset,
        _In_ size_t                length,
        _In_ OSHandle_t*           shm)
{
    return SHMConform(
            handle,
            &(SHMConformityOptions_t) {
                .BufferAlignment = fsBaseContext->IOBufferAlignment,
                .Conformity = fsBaseContext->IOConformity
            },
            SHM_CONFORM_BACKFILL_ON_UNMAP,
            SHM_ACCESS_READ | SHM_ACCESS_WRITE,
            offset,
            length,
            shm
    );
}

void ctt_filesystem_read_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx,
        const struct ctt_fs_transfer_params* params)
{
    oserr_t    oserr;
    OSHandle_t shm;
    size_t     read;
    TRACE("ctt_filesystem_read_invocation()");

    oserr = __MapUserBufferRead(
            (void*)fsctx,
            params->buffer_id,
            (size_t)params->offset,
            (size_t)params->count,
            &shm
    );
    if (oserr != OS_EOK) {
        ctt_filesystem_read_response(message, oserr, 0);
        return;
    }

    oserr = FsRead(
            (void*)fsctx,
            (void*)fctx,
            // Important (!) to note here that we are reading the effective buffer values which may
            // have changed after calling SHMConform. We don't know anymore whether we have
            // the original buffer anymore.
            shm.ID,
            SHMBuffer(&shm),
            SHMBufferOffset(&shm),
            (size_t)params->count, // TODO: be consistent with size units
            &read
    );
    ctt_filesystem_read_response(message, oserr, read);
    OSHandleDestroy(&shm);
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

static inline oserr_t
__MapUserBufferWrite(
        _In_ struct FSBaseContext* fsBaseContext,
        _In_ uuid_t                handle,
        _In_ size_t                offset,
        _In_ size_t                length,
        _In_ OSHandle_t*           shm)
{
    return SHMConform(
            handle,
            &(SHMConformityOptions_t) {
                    .BufferAlignment = fsBaseContext->IOBufferAlignment,
                    .Conformity = fsBaseContext->IOConformity
            },
            SHM_CONFORM_FILL_ON_CREATION,
            SHM_ACCESS_READ,
            offset,
            length,
            shm
    );
}

void ctt_filesystem_write_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx, const struct ctt_fs_transfer_params* params)
{
    oserr_t    oserr;
    OSHandle_t shm;
    size_t     written;
    TRACE("ctt_filesystem_write_invocation()");

    oserr = __MapUserBufferWrite(
            (void*)fsctx,
            params->buffer_id,
            (size_t)params->offset,
            (size_t)params->count,
            &shm
    );
    if (oserr != OS_EOK) {
        ctt_filesystem_write_response(message, oserr, 0);
        return;
    }

    oserr = FsWrite(
            (void*)fsctx,
            (void*)fctx,
            // Important (!) to note here that we are reading the effective buffer values which may
            // have changed after calling SHMConform. We don't know anymore whether we have
            // the original buffer anymore.
            shm.ID,
            SHMBuffer(&shm),
            SHMBufferOffset(&shm),
            (size_t)params->count, // TODO: be consistent with size units
            &written
    );
    ctt_filesystem_write_response(message, oserr, written);
    OSHandleDestroy(&shm);
}

extern oserr_t
FsMove(
        _In_ void*      instanceData,
        _In_ mstring_t* from,
        _In_ mstring_t* to,
        _In_ int        copy);

void ctt_filesystem_move_invocation(struct gracht_message* message, const uintptr_t fsctx, const char* source, const char* target, const uint8_t copy)
{
    oserr_t    oserr;
    mstring_t* msource;
    mstring_t* mdestination;
    TRACE("ctt_filesystem_move_invocation()");

    msource = mstr_new_u8(source);
    if (msource == NULL) {
        ctt_filesystem_move_response(message, OS_EOOM);
        return;
    }

    mdestination = mstr_new_u8(target);
    if (mdestination == NULL) {
        mstr_delete(msource);
        ctt_filesystem_move_response(message, OS_EOOM);
        return;
    }

    oserr = FsMove(
            (void*)fsctx,
            msource,
            mdestination,
            copy
    );
    ctt_filesystem_move_response(message, oserr);
    mstr_delete(msource);
    mstr_delete(mdestination);
}

extern oserr_t
FsTruncate(
        _In_ void*    instanceData,
        _In_ void*    data,
        _In_ uint64_t size);

void ctt_filesystem_truncate_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx, const uint64_t size)
{
    oserr_t oserr;
    TRACE("ctt_filesystem_truncate_invocation()");

    oserr = FsTruncate((void*)fsctx, (void*)fctx, size);
    ctt_filesystem_truncate_response(message, oserr);
}

extern oserr_t
FsSeek(
        _In_  void*     instanceData,
        _In_  void*     data,
        _In_  uint64_t  absolutePosition,
        _Out_ uint64_t* absolutePositionOut);

void ctt_filesystem_seek_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx, const uint64_t position)
{
    uint64_t positionResult;
    oserr_t  oserr;
    TRACE("ctt_filesystem_seek_invocation()");

    oserr = FsSeek(
            (void*)fsctx,
            (void*)fctx,
            position,
            &positionResult
    );
    ctt_filesystem_seek_response(message, oserr, position);
}
