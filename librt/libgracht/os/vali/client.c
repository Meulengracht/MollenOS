/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Gracht Vali Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

// Link operations not supported in a packet-based environment
// - Events
// - Stream

#include <ds/streambuffer.h>
#include <ddk/bytepool.h>
#include <errno.h>
#include <internal/_syscalls.h>
#include "../../include/gracht/link/vali.h"
#include "../../include/gracht/debug.h"
#include <os/dmabuf.h>
#include <os/mollenos.h>
#include <stdlib.h>
#include <string.h>

#define GRACHT_MESSAGE_THRESHOLD 128

struct vali_link_manager {
    struct client_link_ops ops;
    struct dma_attachment  dma;
    bytepool_t*            pool;
};

static int vali_link_connect(struct client_link_ops* linkManager)
{
    errno = (ENOTSUP);
    return 0;
}

static int vali_link_send_packet(struct vali_link_manager* linkManager,
    struct gracht_message* messageBase, struct vali_link_message* messageContext)
{
    struct ipmsg_desc  message;
    struct ipmsg_desc* messagePointer = &message;
    OsStatus_t         status;
    int                i;
    
    message.address  = &messageContext->address;
    message.response = &messageContext->response;
    message.base     = messageBase;
    
    if (messageBase->header.length > GRACHT_MAX_MESSAGE_SIZE) {
        for (i = 0; messageBase->header.length > GRACHT_MAX_MESSAGE_SIZE && i < messageBase->header.param_in; i++) {
            if (messageBase->params[i].length > GRACHT_MESSAGE_THRESHOLD) {
                messageBase->params[i].type = GRACHT_PARAM_SHM;
                messageBase->header.length -= messageBase->params[i].length;
            }
        }
    }
    
    // Setup the response
    if (messageBase->header.param_out) {
        TRACE("[gracht] [client-link] [vali] allocated DMA buffer [base 0x%llx] 0x%llx, length %u",
            linkManager->dma.buffer, messageContext->base.descriptor, LODWORD(length));
        messageContext->response.dma_handle = linkManager->dma.handle;
        messageContext->response.dma_offset = LOWORD(
            ((uintptr_t)messageContext->base.descriptor - (uintptr_t)linkManager->dma.buffer));
    }
    
    status = Syscall_IpcContextSend(&messagePointer, 1, 0);
    if (status != OsSuccess) {
        OsStatusToErrno(status);
        return GRACHT_MESSAGE_ERROR;
    }
    return GRACHT_MESSAGE_COMPLETED;
}

static int vali_link_recv(struct client_link_ops* linkManager, void* messageBuffer,
    unsigned int flags, struct gracht_message** messageOut)
{
    if (!linkManager) {
        errno = (EINVAL);
        return -1;
    }
    
    errno = (ENOTSUP);
    return -1;
}

static void vali_link_destroy(struct vali_link_manager* linkManager)
{
    if (!linkManager) {
        return;
    }
    
    dma_attachment_unmap(&linkManager->dma);
    dma_detach(&linkManager->dma);
    free(linkManager->pool);
    free(linkManager);
}

static int vali_link_allocate_buffer(struct vali_link_manager* linkManager,
    size_t bufferLength, void** transferBufferOut)
{
    void* buffer = bget(linkManager->pool, bufferLength);
    if (!buffer) {
        errno = (ENOMEM);
        return -1;
    }
    
    *transferBufferOut = buffer;
    return 0;
}

static void vali_link_free_buffer(struct vali_link_manager* linkManager,
    void* transferBuffer)
{
    brel(linkManager->pool, transferBuffer);
}

int gracht_link_vali_client_create(struct client_link_ops** linkOut)
{
    struct vali_link_manager* linkManager;
    OsStatus_t                status;
    struct dma_buffer_info    bufferInfo;
    
    linkManager = (struct vali_link_manager*)malloc(sizeof(struct vali_link_manager));
    if (!linkManager) {
        ERROR("[gracht] [client-link] [vali] failed to allocate memory");
        errno = (ENOMEM);
        return -1;
    }
    
    bufferInfo.name     = "gracht_ipc";
    bufferInfo.length   = 0x1000;
    bufferInfo.capacity = 0x1000;
    bufferInfo.flags    = 0;
    
    // if client
    // create dma memory pool for responses
    status = dma_create(&bufferInfo, &linkManager->dma);
    if (status != OsSuccess) {
        ERROR("[gracht] [client-link] [vali] failed to allocate DMA area");
        OsStatusToErrno(status);
        free(linkManager);
        return -1;
    }
    
    status = bpool(linkManager->dma.buffer, linkManager->dma.length, &linkManager->pool);
    if (status != OsSuccess) {
        ERROR("[gracht] [client-link] [vali] failed to create DMA pool");
        dma_attachment_unmap(&linkManager->dma);
        dma_detach(&linkManager->dma);
        free(linkManager);
        return -1;
    }
    
    linkManager->ops.get_buffer  = (client_link_get_buffer_fn)vali_link_allocate_buffer;
    linkManager->ops.free_buffer = (client_link_free_buffer_fn)vali_link_free_buffer;
    linkManager->ops.connect     = vali_link_connect;
    linkManager->ops.recv        = vali_link_recv;
    linkManager->ops.send        = (client_link_send_fn)vali_link_send_packet;
    linkManager->ops.destroy     = (client_link_destroy_fn)vali_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
