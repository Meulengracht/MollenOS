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
#include <gracht/link/vali.h>
#include <os/dmabuf.h>

static struct vali_link_manager {
    struct client_link_ops ops;
    struct dma_attachment  dma;
    bytepool_t*            pool;
};

static int vali_link_connect(struct client_link_ops* linkManager)
{
    _set_errno(ENOTSUP);
    return -1;
}

static int vali_link_unpack_response(void* buffer, struct gracht_message* message)
{
    char* pointer = buffer;
    int   i;
    
    for (i = 0; i < messageBase->param_out; i++) {
        struct gracht_param* param = &messageBase->params[messageBase->param_in + i];
        
        if (param->type == PARAM_VALUE) {
            if (param->data.buffer.length == 1) {
                *((uint8_t*)param->data.buffer.pointer) = *((uint8_t*)pointer);
            }
            else if (param->data.buffer.length == 2) {
                *((uint16_t*)param->data.buffer.pointer) = *((uint16_t*)pointer);
            }
            else if (param->data.buffer.length == 4) {
                *((uint32_t*)param->data.buffer.pointer) = *((uint32_t*)pointer);
            }
            else if (param->data.buffer.length == 8) {
                *((uint64_t*)param->data.buffer.pointer) = *((uint64_t*)pointer);
            }
        }
        else if (param->type == PARAM_BUFFER) {
            memcpy(param->data.buffer.pointer, pointer, param->data.buffer.length);
        }
        pointer += param->data.buffer.length;
    }
}

static int vali_link_message_finish(struct vali_link_manager* linkManager
    struct vali_link_message* messageContext)
{
    brel(linkManager->pool, messageContext->response_buffer);
    return 0;
}

static int vali_link_send_packet(struct vali_link_manager* linkManager,
    struct gracht_message* messageBase, struct vali_link_message* messageContext)
{
    struct ipmsg_desc message;
    OsStatus_t        status;
    int               i;
    
    message.flags    = flags;
    message.address  = &messageContext->address;
    message.response = &messageContext->response;
    message.base     = (struct ipmsg_base*)messageBase;
    
    // Setup the response
    if (messageBase->param_out) {
        size_t length = 0;
        for (i = 0; i < messageBase->param_out; i++) {
            iov_out[i].iov_base = messageBase->params[messageBase->param_in + i].data.buffer;
            iov_out[i].iov_len  = messageBase->params[messageBase->param_in + i].data.length;
            length += messageBase->params[messageBase->param_in + i].data.length;
        }
        
        messageContext->response_buffer = bget(linkManager->pool, length);
        if (!messageContext->response_buffer) {
            _set_errno(ENOMEM); // support bget growth?
            return -1;
        }
        
        message.ResponseDma       = linkManager.dma.handle;
        message.ResponseDmaOffset = LOWORD(((uintptr_t)messageContext->response_buffer - (uintptr_t)linkManager.dma.buffer));
    }
    
    status = Syscall_IpcContextSend(&message, 0);
    if (messageBase->param_out && (messageBase->flags & MESSAGE_CONFIG_SYNC)) {
        vali_link_unpack_response(messageContext->response_buffer, messageBase);
        vali_link_message_finish(linkManager, messageContext);
    }
    
    return OsStatusToErrno(status);
}

static int vali_link_recv(struct client_link_ops* linkManager, struct gracht_recv_message* message, unsigned int flags)
{
    if (!linkManager) {
        _set_errno(EINVAL);
        return -1;
    }
    
    _set_errno(ENOTSUP);
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

int gracht_link_vali_client_create(struct client_link_ops** linkOut)
{
    struct vali_link_manager* linkManager;
    OsStatus_t                status;
    struct dma_buffer_info    bufferInfo;
    
    linkManager = (struct vali_link_manager*)malloc(sizeof(struct vali_link_manager));
    if (!linkManager) {
        _set_errno(ENOMEM);
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
        OsStatusToErrno(status);
        free(linkManager);
        return -1;
    }
    
    status = bpool(linkManager->dma.buffer, 0x1000, &linkManager->pool);
    if (status != OsSuccess) {
        dma_attachment_unmap(&linkManager->dma);
        dma_detach(&linkManager->dma);
        free(linkManager);
        return -1;
    }
    
    linkManager->ops.connect = vali_link_connect;
    linkManager->ops.recv    = vali_link_recv;
    linkManager->ops.send    = vali_link_send_packet;
    linkManager->ops.destroy = vali_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
