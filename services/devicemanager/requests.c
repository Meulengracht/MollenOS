/**
 * Copyright 2021, Philip Meulengracht
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
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <gracht/server.h>
#include <gracht/client.h>
#include <os/usched/usched.h>
#include <stdlib.h>
#include <string.h>
#include "requests.h"

#include <sys_device_service_server.h>

extern void DmHandleNotify(Request_t* request, void*);
extern void DmHandleDeviceCreate(Request_t* request, void*);
extern void DmHandleDeviceDestroy(Request_t* request, void*);
extern void DmHandleGetDevicesByProtocol(Request_t* request, void*);
extern void DmHandleIoctl(Request_t* request, void*);
extern void DmHandleIoctl2(Request_t* request, void*);
extern void DmHandleRegisterProtocol(Request_t* request, void*);

static _Atomic(UUId_t) g_requestId = ATOMIC_VAR_INIT(1);

static inline const void* memdup(const void* source, size_t count) {
    void* dest;
    if (!count) {
        return NULL;
    }
    dest = malloc(count);
    if (!dest) {
        return NULL;
    }
    memcpy(dest, source, count);
    return dest;
}

static Request_t*
CreateRequest(struct gracht_message* message)
{
    Request_t* request;
    size_t     requestSize = sizeof(Request_t);

    if (message) {
        requestSize += GRACHT_MESSAGE_DEFERRABLE_SIZE(message);
    }

    request = malloc(requestSize);
    if (!request) {
        ERROR("CreateRequest out of memory for message allocation!");
        return NULL;
    }

    request->id = atomic_fetch_add(&g_requestId, 1);
    request->state = RequestState_CREATED;
    gracht_server_defer_message(message, &request->message[0]);
    usched_cnd_init(&request->signal);
    ELEMENT_INIT(&request->leaf, 0, request);
    return request;
}

void RequestDestroy(Request_t* request)
{
    assert(request != NULL);

    free(request);
}

void RequestSetState(Request_t* request, enum RequestState state)
{
    assert(request != NULL);

    request->state = state;
}

void sys_device_notify_invocation(struct gracht_message* message,
                                  const UUId_t driverId, const UUId_t driverHandle)
{
    Request_t* request;
    TRACE("sys_device_notify_invocation()");

    request = CreateRequest(message);
    if (!request) {
        ERROR("sys_device_notify_invocation out of memory for request!");
        return;
    }

    // initialize parameters
    request->parameters.notify.driver_id     = driverId;
    request->parameters.notify.driver_handle = driverHandle;
    usched_task_queue((usched_task_fn)DmHandleNotify, request);
}

void sys_device_register_invocation(struct gracht_message* message,
        const uint8_t* buffer, const uint32_t buffer_count, const unsigned int flags)
{
    Request_t* request;
    TRACE("sys_device_register_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_device_register_response(message, OsOutOfMemory, UUID_INVALID);
        return;
    }

    // initialize parameters
    request->parameters.create.device_buffer = (uint8_t*)memdup(buffer, buffer_count);
    request->parameters.create.buffer_size   = buffer_count;
    request->parameters.create.flags         = flags;
    usched_task_queue((usched_task_fn)DmHandleDeviceCreate, request);
}

void sys_device_unregister_invocation(struct gracht_message* message, const UUId_t deviceId)
{
    Request_t* request;
    TRACE("sys_device_unregister_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_device_unregister_response(message, OsOutOfMemory);
        return;
    }

    // initialize parameters
    request->parameters.destroy.device_id = deviceId;
    usched_task_queue((usched_task_fn)DmHandleDeviceDestroy, request);
}

void sys_device_ioctl_invocation(struct gracht_message* message,
        const UUId_t deviceId, const unsigned int command, const unsigned int flags)
{
    Request_t* request;
    TRACE("sys_device_ioctl_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_device_ioctl_response(message, OsOutOfMemory);
        return;
    }

    // initialize parameters
    request->parameters.ioctl.device_id = deviceId;
    request->parameters.ioctl.command   = command;
    request->parameters.ioctl.flags     = flags;
    usched_task_queue((usched_task_fn)DmHandleIoctl, request);
}

void sys_device_ioctlex_invocation(struct gracht_message* message, const UUId_t deviceId,
                                   const int direction, const unsigned int command,
                                   const size_t value, const unsigned int width)
{
    Request_t* request;
    TRACE("sys_device_ioctlex_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_device_ioctlex_response(message, OsOutOfMemory, 0);
        return;
    }

    // initialize parameters
    request->parameters.ioctl2.device_id = deviceId;
    request->parameters.ioctl2.direction = direction;
    request->parameters.ioctl2.command   = command;
    request->parameters.ioctl2.value     = value;
    request->parameters.ioctl2.width     = width;
    usched_task_queue((usched_task_fn)DmHandleIoctl2, request);
}

void sys_device_get_devices_by_protocol_invocation(struct gracht_message* message, const uint8_t protocolId)
{
    Request_t* request;
    TRACE("sys_device_get_devices_by_protocol_invocation()");

    request = CreateRequest(message);
    if (!request) {
        sys_device_ioctlex_response(message, OsOutOfMemory, 0);
        return;
    }

    // initialize parameters
    request->parameters.get_devices_by_protocol.protocol = protocolId;
    usched_task_queue((usched_task_fn)DmHandleGetDevicesByProtocol, request);
}

void ctt_driver_event_device_protocol_invocation(gracht_client_t* client,
                                                 const UUId_t deviceId,
                                                 const char* protocolName,
                                                 const uint8_t protocolId)
{
    Request_t* request;
    TRACE("ctt_driver_event_device_protocol_invocation()");

    request = CreateRequest(NULL);
    if (!request) {
        return;
    }

    // initialize parameters
    request->parameters.register_protocol.device_id     = deviceId;
    request->parameters.register_protocol.protocol_id   = protocolId;
    request->parameters.register_protocol.protocol_name = strdup(protocolName);
    usched_task_queue((usched_task_fn)DmHandleRegisterProtocol, request);
}
