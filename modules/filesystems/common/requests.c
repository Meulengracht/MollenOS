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

#include <ctt_filesystem_service_server.h>

extern void FsInitializeWrapper(void*,void*);
extern void FsDestroyWrapper(void*,void*);
extern void FsOpenWrapper(void*,void*);
extern void FsCreateWrapper(void*,void*);
extern void FsCloseWrapper(void*,void*);
extern void FsStatWrapper(void*,void*);
extern void FsLinkWrapper(void*,void*);
extern void FsUnlinkWrapper(void*,void*);
extern void FsReadLinkWrapper(void*,void*);
extern void FsMoveWrapper(void*,void*);
extern void FsReadWrapper(void*,void*);
extern void FsWriteWrapper(void*,void*);
extern void FsTruncateWrapper(void*,void*);
extern void FsSeekWrapper(void*,void*);

static _Atomic(uuid_t) g_requestId = ATOMIC_VAR_INIT(1);

static FileSystemRequest_t*
CreateRequest(struct gracht_message* message)
{
    FileSystemRequest_t* request;

    request = malloc(sizeof(FileSystemRequest_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!request) {
        ERROR("CreateRequest out of memory for message allocation!");
        return NULL;
    }

    request->id = atomic_fetch_add(&g_requestId, 1);
    gracht_server_defer_message(message, &request->message[0]);
    usched_mtx_init(&request->lock);
    usched_cnd_init(&request->signal);
    return request;
}

void FSRequestDestroy(FileSystemRequest_t* request)
{
    assert(request != NULL);

    free(request);
}

void ctt_filesystem_setup_invocation(struct gracht_message* message, const struct ctt_fs_setup_params* params)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_setup_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_setup_response(message, OS_EOOM, 0);
        return;
    }

    from_fs_setup_params(params, &request->parameters.init);
    usched_job_queue((usched_task_fn)FsInitializeWrapper, request);
}

void ctt_filesystem_destroy_invocation(struct gracht_message* message, const uintptr_t fsctx)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_destroy_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_destroy_response(message, OS_EOOM);
        return;
    }

    request->parameters.destroy.context = (void*)fsctx;
    usched_job_queue((usched_task_fn)FsDestroyWrapper, request);
}

void ctt_filesystem_fsstat_invocation(struct gracht_message* message, const uintptr_t fsctx)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_fsstat_invocation()");
    request = CreateRequest(message);
    if (!request) {
        struct ctt_fsstat zero;
        ctt_fsstat_init(&zero);
        ctt_filesystem_fsstat_response(message, OS_EOOM, &zero);
        return;
    }

    request->parameters.stat.fscontext = (void*)fsctx;
    usched_job_queue((usched_task_fn)FsStatWrapper, request);
}

void ctt_filesystem_open_invocation(struct gracht_message* message, const uintptr_t fsctx, const char* path)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_open_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_open_response(message, OS_EOOM, 0);
        return;
    }

    request->parameters.open.context = (void*)fsctx;
    request->parameters.open.path = mstr_new_u8(path);
    assert(request->parameters.open.path != NULL);
    usched_job_queue((usched_task_fn)FsOpenWrapper, request);
}

void ctt_filesystem_create_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx, const struct ctt_fs_open_params* params)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_create_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_create_response(message, OS_EOOM, 0);
        return;
    }

    request->parameters.create.fscontext = (void*)fsctx;
    request->parameters.create.fcontext = (void*)fctx;
    request->parameters.create.name = mstr_new_u8(params->name);
    assert(request->parameters.create.name != NULL);
    request->parameters.create.owner = params->owner;
    request->parameters.create.flags = params->flags;
    request->parameters.create.permissions = params->permissions;
    usched_job_queue((usched_task_fn)FsCreateWrapper, request);
}

void ctt_filesystem_close_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_close_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_close_response(message, OS_EOOM);
        return;
    }

    request->parameters.close.fscontext = (void*)fsctx;
    request->parameters.close.fcontext = (void*)fctx;
    usched_job_queue((usched_task_fn)FsCloseWrapper, request);
}

void ctt_filesystem_link_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx,
        const char* name, const char* target, const uint8_t symbolic)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_link_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_link_response(message, OS_EOOM);
        return;
    }

    request->parameters.link.fscontext = (void*)fsctx;
    request->parameters.link.fcontext = (void*)fctx;
    request->parameters.link.name = mstr_new_u8(name);
    assert(request->parameters.link.name != NULL);
    request->parameters.link.target = mstr_new_u8(target);
    assert(request->parameters.link.target != NULL);
    request->parameters.link.symbolic = symbolic;
    usched_job_queue((usched_task_fn)FsLinkWrapper, request);
}

void ctt_filesystem_unlink_invocation(struct gracht_message* message, const uintptr_t fsctx, const char* path)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_unlink_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_unlink_response(message, OS_EOOM);
        return;
    }

    request->parameters.unlink.fscontext = (void*)fsctx;
    request->parameters.unlink.path = mstr_new_u8(path);
    assert(request->parameters.unlink.path != NULL);
    usched_job_queue((usched_task_fn)FsUnlinkWrapper, request);
}

void ctt_filesystem_readlink_invocation(struct gracht_message* message, const uintptr_t fsctx, const char* path)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_readlink_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_readlink_response(message, OS_EOOM, NULL);
        return;
    }

    request->parameters.readlink.fscontext = (void*)fsctx;
    request->parameters.readlink.path = mstr_new_u8(path);
    assert(request->parameters.readlink.path != NULL);
    usched_job_queue((usched_task_fn)FsReadLinkWrapper, request);
}

void ctt_filesystem_read_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx,
        const struct ctt_fs_transfer_params* params)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_read_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_read_response(message, OS_EOOM, 0);
        return;
    }

    request->parameters.transfer.fscontext = (void*)fsctx;
    request->parameters.transfer.fcontext = (void*)fctx;
    request->parameters.transfer.buffer_id = params->buffer_id;
    request->parameters.transfer.offset = params->offset;
    request->parameters.transfer.count = params->count;
    usched_job_queue((usched_task_fn)FsReadWrapper, request);
}

void ctt_filesystem_write_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx, const struct ctt_fs_transfer_params* params)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_write_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_write_response(message, OS_EOOM, 0);
        return;
    }

    request->parameters.transfer.fscontext = (void*)fsctx;
    request->parameters.transfer.fcontext = (void*)fctx;
    request->parameters.transfer.buffer_id = params->buffer_id;
    request->parameters.transfer.offset = params->offset;
    request->parameters.transfer.count = params->count;
    usched_job_queue((usched_task_fn)FsWriteWrapper, request);
}

void ctt_filesystem_move_invocation(struct gracht_message* message, const uintptr_t fsctx, const char* source, const char* target, const uint8_t copy)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_move_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_move_response(message, OS_EOOM);
        return;
    }

    request->parameters.move.fscontext = (void*)fsctx;
    request->parameters.move.from = mstr_new_u8(source);
    assert(request->parameters.move.from != NULL);
    request->parameters.move.to = mstr_new_u8(target);
    assert(request->parameters.move.to != NULL);
    request->parameters.move.copy = copy;
    usched_job_queue((usched_task_fn)FsMoveWrapper, request);
}

void ctt_filesystem_truncate_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx, const uint64_t size)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_truncate_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_truncate_response(message, OS_EOOM);
        return;
    }

    request->parameters.truncate.fscontext = (void*)fsctx;
    request->parameters.truncate.fcontext = (void*)fctx;
    request->parameters.truncate.size = size;
    usched_job_queue((usched_task_fn)FsTruncateWrapper, request);
}

void ctt_filesystem_seek_invocation(struct gracht_message* message, const uintptr_t fsctx, const uintptr_t fctx, const uint64_t position)
{
    FileSystemRequest_t* request;

    TRACE("ctt_filesystem_seek_invocation()");
    request = CreateRequest(message);
    if (!request) {
        ctt_filesystem_seek_response(message, OS_EOOM, 0);
        return;
    }

    request->parameters.seek.fscontext = (void*)fsctx;
    request->parameters.seek.fcontext = (void*)fctx;
    request->parameters.seek.position = position;
    usched_job_queue((usched_task_fn)FsSeekWrapper, request);
}
