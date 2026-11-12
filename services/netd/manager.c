/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Network Manager (Socket interface)
 * - Contains the implementation of the socket infrastructure in the network
 *   manager. There a lot of different types of sockets, like internet, ipc
 *   and bluetooth to name the popular ones.
 */

//#define __TRACE

#include <ddk/convert/grants.h>
#include <internal/_service.h>
#include <internal/_tls.h>
#include <os/notification_queue.h>
#include <os/handle.h>
#include <os/grants.h>
#include <ddk/utils.h>
#include "domains/domains.h"
#include <inet/local.h>
#include "manager.h"
#include "socket.h"
#include <stdlib.h>
#include <string.h>

#include "sys_socket_service_server.h"

// Hold this lock from socket lookup through last use, including peer/address
// lookups and destruction. Take it before SyncObject. It must be recursive
// because domain accept calls back into create/pair. Callbacks may defer replies,
// but socket pointers are only valid until the operation ends.
static mtx_t      g_socketExecution = MUTEX_INIT(mtx_recursive);
// Local system sockets. The domains keep track of remote sockets.
static rb_tree_t  g_sockets;
static OSHandle_t g_socketSet;
static OSHandle_t g_socketRegistration;
static thrd_t     g_socketMonitorHandle;

static oserr_t NetworkManagerSocketShutdownLocked(uuid_t, int);

static void
__CleanupSockets(void)
{
    uuid_t   resources[32];
    uint32_t count;
    oserr_t  status;

    // Use the same registration queue as filed: events identify resources whose
    // final reference has closed. No socket-specific kernel notification is
    // needed. The execution lock excludes RPC work and peer lookups while those
    // resources are removed from the socket table.
    do {
        status = OSHandleReadCleanup(
            &g_socketRegistration,
            resources,
            32,
            &count
        );
        if (status != OS_EOK) {
            WARNING("socket cleanup read failed: %u", status);
            return;
        }
        
        for (uint32_t i = 0; i < count; i++) {
            status = NetworkManagerSocketShutdownLocked(
                resources[i],
                SYS_CLOSE_OPTIONS_DESTROY
            );
            if (status != OS_EOK && status != OS_ENOENT) {
                WARNING("socket cleanup failed for %u: %u", resources[i], status);
            }
        }
    } while (count != 0);
}


// All queue links are protected by g_socketExecution. Entries are embedded in
// sockets, so duplicate notifications cannot allocate or grow pending work.
static SocketWorkQueue_t g_runnable;

static void
RemoveSocketWork(
    _In_ Socket_t* socket)
{
    SocketWorkQueue_t* queue = socket->WorkQueue;
    if (!queue) {
        return;
    }

    if (socket->WorkPrevious) {
        socket->WorkPrevious->WorkNext = socket->WorkNext;
    } else {
        queue->Head = socket->WorkNext;
    }

    if (socket->WorkNext) {
        socket->WorkNext->WorkPrevious = socket->WorkPrevious;
    } else {
        queue->Tail = socket->WorkPrevious;
    }

    socket->WorkQueue = NULL;
    socket->WorkPrevious = socket->WorkNext = NULL;
}

static void
AppendSocketWork(
    _In_ SocketWorkQueue_t* queue,
    _In_ Socket_t*          socket)
{
    if (socket->WorkQueue) {
        return;
    }

    socket->WorkQueue = queue;
    socket->WorkPrevious = queue->Tail;
    if (queue->Tail) {
        queue->Tail->WorkNext = socket;
    } else {
        queue->Head = socket;
    }
    queue->Tail = socket;
}

void 
NetworkManagerSocketWaitForCredit(
    _In_ Socket_t* sender,
    _In_ Socket_t* receiver)
{
    AppendSocketWork(&receiver->ReceiveWaiters, sender);
}

static void
WakeReceiveWaiters(
    _In_ Socket_t* receiver)
{
    Socket_t* sender;
    while ((sender = receiver->ReceiveWaiters.Head) != NULL) {
        RemoveSocketWork(sender);
        AppendSocketWork(&g_runnable, sender);
    }
}

static void
NotifyRunnableSockets(void)
{
    if (g_runnable.Head) {
        OSNotificationQueuePost(&g_runnable.Head->Handle, IOSETOUT);
    }
}

static void
HandleSocketEventLocked(
    _In_ struct ioset_event* event)
{
    Socket_t* socket = NetworkManagerSocketGetUnsafe(event->data.handle);
    if (!socket) {
        return;
    }

    if (event->events & IOSETCREDIT) {
        WakeReceiveWaiters(socket);
    }

    if ((event->events & IOSETOUT) && !socket->Configuration.Passive) {
        // A new send cannot make a full receive pipe writable.
        AppendSocketWork(&g_runnable, socket);
    }
}

static void
RunSocketWorkLocked(void)
{
    for (int i = 0; i < NETWORK_MANAGER_MONITOR_MAX_EVENTS && g_runnable.Head; i++) {
        Socket_t* socket = g_runnable.Head;
        RemoveSocketWork(socket);
        mtx_lock(&socket->SyncObject);
        if (!atomic_load(&socket->PendingPackets)) {
            oserr_t status = DomainSend(socket);
            if (status == OS_EINPROGRESS) {
                AppendSocketWork(&g_runnable, socket);
            } else if (status != OS_EOK) {
                ERROR("[socket_monitor] failed to send message: %u", status);
            }
        }
        mtx_unlock(&socket->SyncObject);
    }
    // Schedule another batch if work remains, even if no more send events arrive.
    // Return to the event queue between batches.
    NotifyRunnableSockets();
}

// socket_monitor thread:
// 1. Acquire a transfer buffer for protocol headers + payload size
//    1.1 If out of buffers, create a pending state and set PendingPackets to 1
//    1.2 Exit
// 2. Read payload data into buffer
// 3. Add to packet queue
static int
SocketMonitor(
    _In_ void* Context)
{
    struct ioset_event* Events;
    int                 EventCount;
    oserr_t             oserr;
    
    _CRT_UNUSED(Context);
    TRACE("[socket monitor] starting");
    
    Events = malloc(sizeof(struct ioset_event) * NETWORK_MANAGER_MONITOR_MAX_EVENTS);
    if (!Events) {
        return ENOMEM;
    }
    
    for (;;) {
        OSAsyncContext_t* asyncContext = __tls_current()->async_context;
        if (asyncContext) {
            OSAsyncContextInitialize(asyncContext);
        }
        
        EventCount = 0;
        oserr = OSNotificationQueueWait(
                &g_socketSet,
                Events,
                NETWORK_MANAGER_MONITOR_MAX_EVENTS,
                0,
                NULL,
                &EventCount,
                asyncContext
        );
        if (oserr != OS_EOK && oserr != OS_EINTERRUPTED) {
            ERROR("[socket_monitor] OSNotificationQueueWait FAILED: %u", oserr);
            continue;
        }
        
        if (mtx_lock(&g_socketExecution) != thrd_success) {
            ERROR("[socket_monitor] failed to acquire socket execution lock");
            continue;
        }

        for (int i = 0; i < EventCount; i++) {
            if (Events[i].data.handle == g_socketRegistration.ID) {
                __CleanupSockets();
            } else {
                HandleSocketEventLocked(&Events[i]);
            }
        }

        RunSocketWorkLocked();
        mtx_unlock(&g_socketExecution);
    }
    return 0;
}

oserr_t
NetworkManagerInitialize(void)
{
    oserr_t oserr;
    int     code;
    
    rb_tree_construct(&g_sockets);
    oserr = OSNotificationQueueCreate(0, &g_socketSet);
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    oserr = OSHandleRegisterProvider(&g_socketSet, &g_socketRegistration);
    if (oserr != OS_EOK) {
        OSHandleDestroy(&g_socketSet);
        return oserr;
    }

    // Spawn the socket monitor thread, the network monitor threads are
    // only spawned once a network card is registered.
    code = thrd_create(&g_socketMonitorHandle, SocketMonitor, NULL);
    if (code != thrd_success) {
        OSHandleDestroy(&g_socketRegistration);
        OSHandleDestroy(&g_socketSet);
        return OS_EUNKNOWN;
    }
    return oserr;
}

static oserr_t
NetworkManagerSocketCreateLocked(
    _In_  int                      Domain,
    _In_  int                      Type,
    _In_  int                      Protocol,
    _Out_ uuid_t*                  HandleOut,
    _Out_ struct sys_shm_transfer* RecvBufferOut,
    _Out_ struct sys_shm_transfer* SendBufferOut)
{
    Socket_t*          socket;
    oserr_t            oserr;
    struct ioset_event event;
    
    TRACE("[net_manager] [create] %i, %i, %i", Domain, Type, Protocol);
    
    if (Domain >= AF_MAX || Domain < 0) {
        WARNING("Invalid Domain type specified %i", Domain);
        return OS_EINVALPARAMS;
    }

    oserr = SocketCreateImpl(&g_socketRegistration, Domain, Type, Protocol, &socket);
    if (oserr != OS_EOK) {
        WARNING("SocketCreateImpl failed with %u", oserr);
        return oserr;
    }
    
    // Add it to the handle set
    event.events = IOSETOUT | IOSETCREDIT;
    event.data.handle = (uuid_t)(uintptr_t)socket->Header.key;
    oserr = OSNotificationQueueCtrl(
            &g_socketSet, IOSET_ADD,
            &socket->Handle, &event);
    if (oserr != OS_EOK) {
        SocketShutdownImpl(socket, SYS_CLOSE_OPTIONS_DESTROY);
        return oserr;
    }
    
    rb_tree_append(&g_sockets, &socket->Header);
    *HandleOut           = (uuid_t)(uintptr_t)socket->Header.key;
    __ToSysSHMTransfer(&socket->Send.Transfer, SendBufferOut);
    __ToSysSHMTransfer(&socket->Receive.Transfer, RecvBufferOut);
    TRACE("[net_manager] [create] => %u", *HandleOut);
    return oserr;
}

oserr_t
NetworkManagerSocketCreate(
        _In_  int     Domain,
        _In_  int     Type,
        _In_  int     Protocol,
        _Out_ uuid_t* HandleOut,
        _Out_ struct sys_shm_transfer* RecvBufferOut,
        _Out_ struct sys_shm_transfer* SendBufferOut)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketCreateLocked(Domain, Type, Protocol,
                                             HandleOut, RecvBufferOut, SendBufferOut);
    mtx_unlock(&g_socketExecution);
    return status;
}

static oserr_t
__SocketAddressValidate(
    _In_ const uint8_t* address,
    _In_ uint32_t       length)
{
    const struct sockaddr* socketAddress = (const struct sockaddr*)address;

    // Local addresses can be shorter than struct sockaddr. Check the common
    // prefix first, then constrain the embedded length to the decoded bytes.
    if (length < offsetof(struct sockaddr, sa_data) || length > sizeof(struct sockaddr_storage)) {
        return OS_EINVALPARAMS;
    }

    if (socketAddress->sa_len != length) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__SocketCaller(
    _In_  const struct gracht_message* message,
    _Out_ uint64_t*                    groupOut)
{
    const struct gracht_security_context* caller;

    caller = gracht_server_get_security_context(message);
    if (!__IsAuthenticatedCaller(caller)) {
        return OS_EPERMISSIONS;
    }
    *groupOut = caller->principal;
    return OS_EOK;
}

static oserr_t
__SocketRequestBegin(
    _In_  const struct gracht_message* message,
    _In_  uuid_t                       reference,
    _Out_ uuid_t*                      resourceOut)
{
    uint64_t group;
    oserr_t  status;

    status = __SocketCaller(message, &group);
    if (status != OS_EOK) {
        return status;
    }
    
    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }

    // Validation does not keep the service object alive. The execution lock
    // therefore stays held through the operation, excluding final cleanup.
    status = OSHandleValidate(
        &g_socketRegistration,
        group,
        reference,
        OS_SOCKET_ACCESS,
        resourceOut
    );
    if (status != OS_EOK) {
        mtx_unlock(&g_socketExecution);
    }
    return status;
}

static oserr_t
__SocketDescribe(
    _In_  Socket_t*               socket,
    _Out_ struct sys_socket_info* infoOut)
{
    struct sys_socket_info info = {
        .domain = socket->DomainType,
        .type = socket->Type,
        .protocol = socket->Protocol
    };
    oserr_t status;

    // These reusable tokens were created with the rings. Describing a socket
    // allocates no export and adds no socket reference, including on retries.
    status = __ToSysSHMTransfer(&socket->Receive.Transfer, &info.recv_buffer);
    if (status != OS_EOK) {
        return status;
    }

    status = __ToSysSHMTransfer(&socket->Send.Transfer, &info.send_buffer);
    if (status == OS_EOK) {
        *infoOut = info;
    }
    return status;
}

oserr_t
NetworkManagerSocketExport(
    _In_  const struct gracht_message* message,
    _In_  uuid_t                       resource,
    _Out_ struct sys_grant_token*      referenceOut,
    _Out_ struct sys_socket_info*      infoOut)
{
    Socket_t*    socket;
    GrantToken_t token;
    uint64_t     group;
    oserr_t      status;

    GrantTokenOptions_t options = {
        .ResourceType = GrantTokenResourceTypeProvider,
        .Scope = OS_SOCKET_ACCESS,
        .TargetType = GrantTokenTargetTypeSpecific,
        .Flags = GrantTokenFlagsSingleUse
    };

    status = __SocketCaller(message, &group);
    if (status != OS_EOK) {
        return status;
    }

    if (group > UINT32_MAX) {
        return OS_EPERMISSIONS;
    }

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }

    socket = NetworkManagerSocketGetUnsafe(resource);
    if (socket == NULL) {
        status = OS_ENOENT;
        goto exit;
    }

    status = __SocketDescribe(socket, infoOut);
    if (status != OS_EOK) {
        goto exit;
    }

    options.TargetGlobalHandleID = (uuid_t)group;
    status = OSGrantTokenCreate(socket->References.Initial.ID, &options, &token);
    if (status != OS_EOK) {
        goto exit;
    }

    status = __ToSysGrantToken(&token, referenceOut);
    if (status != OS_EOK) {
        OSGrantTokenRevoke(token.ControlHandleID);
        goto exit;
    }

    // The token keeps the new socket alive until import. Keeping Initial
    // as well would prevent the kernel's final-reference cleanup event.
    status = OSHandleRelease(&socket->References.Initial);
    if (status != OS_EOK) {
        OSGrantTokenRevoke(token.ControlHandleID);
        goto exit;
    }

    socket->References.ExportControl = token.ControlHandleID;

exit:
    mtx_unlock(&g_socketExecution);
    return status;
}

void
sys_socket_create_invocation(
    struct gracht_message* message,
    const int              domain,
    const int              type,
    const int              protocol)
{
    struct sys_grant_token  reference = { 0 };
    struct sys_socket_info  info = { 0 };
    struct sys_shm_transfer recvBuffer;
    struct sys_shm_transfer sendBuffer;
    uuid_t                  resource = UUID_INVALID;
    uint64_t                group;
    oserr_t                 status;

    status = __SocketCaller(message, &group);
    if (status == OS_EOK) {
        status = NetworkManagerSocketCreate(
            domain,
            type,
            protocol,
            &resource,
            &recvBuffer,
            &sendBuffer
        );
    }

    if (status == OS_EOK) {
        status = NetworkManagerSocketExport(message, resource, &reference, &info);
        if (status != OS_EOK) {
            NetworkManagerSocketShutdown(resource, SYS_CLOSE_OPTIONS_DESTROY);
        }
    }

    if (sys_socket_create_response(message, status, &reference, &info) != 0 && status == OS_EOK) {
        // A failed reply must not leave an unused initial export active. If
        // the client already imported it, revocation preserves that reference.
        (void)OSGrantTokenRevoke(reference.export_id);
    }
}

void
sys_socket_acquire_invocation(
    struct gracht_message* message,
    const uuid_t           handle)
{
    struct sys_socket_info info = { 0 };
    Socket_t*              socket;
    uuid_t                 resource;
    oserr_t                status;

    status = __SocketRequestBegin(message, handle, &resource);
    if (status == OS_EOK) {
        socket = NetworkManagerSocketGetUnsafe(resource);
        status = socket != NULL ? __SocketDescribe(socket, &info) : OS_ENOENT;
        mtx_unlock(&g_socketExecution);
    }
    sys_socket_acquire_response(message, status, &info);
}

static oserr_t
NetworkManagerSocketShutdownLocked(
        _In_ uuid_t Handle,
        _In_ int    Options)
{
    Socket_t* socket;
    oserr_t   oserr;
    
    TRACE("[net_manager] [shutdown] %u, %i", Handle, Options);

    socket = NetworkManagerSocketGetUnsafe(Handle);
    if (!socket) {
        ERROR("[net_manager] [shutdown] invalid handle %u", Handle);
        return OS_ENOENT;
    }
    
    // Before initiating the actual destruction, remove it from our handle list.
    if (Options == SYS_CLOSE_OPTIONS_DESTROY) {
        // The execution lock prevents other socket operations from using this
        // socket during removal and destruction, including through peer lookups.
        if (!rb_tree_remove(&g_sockets, (void*)(uintptr_t)Handle)) {
            return OS_ENOENT;
        }

        RemoveSocketWork(socket);
        WakeReceiveWaiters(socket);
        NotifyRunnableSockets();

        oserr = OSNotificationQueueCtrl(
                &g_socketSet,
                IOSET_DEL,
                &socket->Handle,
                NULL
        );
        if (oserr != OS_EOK) {
            ERROR("[net_manager] [shutdown] failed to remove handle %u from socket set", Handle);
        }
    }
    return SocketShutdownImpl(socket, Options);
}

oserr_t
NetworkManagerSocketShutdown(
        _In_ uuid_t Handle,
        _In_ int    Options)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketShutdownLocked(Handle, Options);
    mtx_unlock(&g_socketExecution);
    return status;
}

void sys_socket_close_invocation(struct gracht_message* message, const uuid_t handle, const enum sys_close_options options)
{
    uuid_t  resource;
    oserr_t status;

    status = __SocketRequestBegin(message, handle, &resource);
    if (status == OS_EOK) {
        // Destruction is driven exclusively by final-reference cleanup. A
        // client close must never destroy the socket used by another process.
        status = options == SYS_CLOSE_OPTIONS_DESTROY ? OS_EINVALPARAMS :
            NetworkManagerSocketShutdown(resource, options);
        mtx_unlock(&g_socketExecution);
    }
    sys_socket_close_response(message, status);
}

static oserr_t
NetworkManagerSocketBindLocked(
        _In_ uuid_t                 Handle,
        _In_ const struct sockaddr* Address)
{
    Socket_t*  Socket;
    oserr_t Status;
    
    TRACE("[net_manager] [bind] %u", Handle);
    
    Socket = NetworkManagerSocketGetUnsafe(Handle);
    if (!Socket) {
        ERROR("[net_manager] [bind] invalid handle %u", Handle);
        return OS_ENOENT;
    }
    
    if (Socket->Configuration.Connecting || Socket->Configuration.Connected) {
        return OS_EINPROGRESS;
    }
    
    Status = DomainUpdateAddress(Socket, Address);
    if (Status == OS_EOK) {
        Socket->Configuration.Bound = 1;
    }
    return Status;
}

oserr_t
NetworkManagerSocketBind(
        _In_ uuid_t                 Handle,
        _In_ const struct sockaddr* Address)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketBindLocked(Handle, Address);
    mtx_unlock(&g_socketExecution);
    return status;
}

void sys_socket_bind_invocation(struct gracht_message* message, const uuid_t handle,
        const uint8_t* address, const uint32_t address_count)
{
    uuid_t  resource;
    oserr_t status;

    status = __SocketRequestBegin(message, handle, &resource);
    if (status == OS_EOK) {
        status = __SocketAddressValidate(address, address_count);
        if (status == OS_EOK) {
            status = NetworkManagerSocketBind(resource, (const struct sockaddr*)address);
        }
        mtx_unlock(&g_socketExecution);
    }
    sys_socket_bind_response(message, status);
}

// On success, the domain sends the reply when the operation completes.
static oserr_t
NetworkManagerSocketConnectLocked(
        _In_ struct gracht_message* message,
        _In_ uuid_t                 handle,
        _In_ const struct sockaddr* address)
{
    Socket_t*  socket;
    oserr_t status;
    ERROR("[net_manager] [connect] %u", handle);
    
    socket = NetworkManagerSocketGetUnsafe(handle);
    if (!socket) {
        ERROR("[net_manager] [connect] invalid handle %u", handle);
        return OS_EHOSTUNREACHABLE;
    }
    
    // If the socket is passive, then we don't allow active actions
    // like connecting to other sockets.
    if (socket->Configuration.Passive) {
        return OS_ENOTSUPPORTED;
    }
    
    // Connection-based sockets can connect only once. Connectionless protocols
    // allow repeated connect calls to change the peer, or AF_UNSPEC to clear it,
    // but that is not supported here yet.
    if (socket->Type == SOCK_STREAM || socket->Type == SOCK_SEQPACKET) {
        if (socket->Configuration.Connecting) {
            return OS_EINPROGRESS;
        }
        
        if (socket->Configuration.Connected) {
            return OS_EISCONNECTED;
        }
    } else if (socket->Type != SOCK_DGRAM) {
        return OS_ENOTSUPPORTED;
    }

    socket->Configuration.Connecting = 1;
    status = DomainConnect(message, socket, address);
    if (status != OS_EOK) {
        socket->Configuration.Connecting = 0;
    }
    return status;
}

oserr_t
NetworkManagerSocketConnect(
        _In_ struct gracht_message* message,
        _In_ uuid_t                 handle,
        _In_ const struct sockaddr* address)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketConnectLocked(message, handle, address);
    mtx_unlock(&g_socketExecution);
    return status;
}

void sys_socket_connect_invocation(struct gracht_message* message, const uuid_t handle,
        const uint8_t* address, const uint32_t address_count)
{
    uuid_t  resource;
    oserr_t status;

    status = __SocketRequestBegin(message, handle, &resource);
    if (status == OS_EOK) {
        status = __SocketAddressValidate(address, address_count);
        if (status == OS_EOK) {
            status = NetworkManagerSocketConnect(message, resource, (const struct sockaddr*)address);
        }
        mtx_unlock(&g_socketExecution);
    }
    if (status != OS_EOK) {
        sys_socket_connect_response(message, status);
    }
}

// On success, the domain sends the reply when the operation completes.
static oserr_t
NetworkManagerSocketAcceptLocked(
        _In_ struct gracht_message* message,
        _In_ uuid_t                 handle)
{
    Socket_t* socket;
    
    ERROR("[net_manager] [accept] %u", handle);
    
    socket = NetworkManagerSocketGetUnsafe(handle);
    if (!socket) {
        ERROR("[net_manager] [accept] invalid handle %u", handle);
        return OS_ENOENT;
    }
    
    if (!socket->Configuration.Passive) {
        return OS_ENOTSUPPORTED;
    }
    return DomainAccept(message, socket);
}

oserr_t
NetworkManagerSocketAccept(
        _In_ struct gracht_message* message,
        _In_ uuid_t                 handle)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketAcceptLocked(message, handle);
    mtx_unlock(&g_socketExecution);
    return status;
}

void sys_socket_accept_invocation(struct gracht_message* message, const uuid_t handle)
{
    uuid_t  resource;
    oserr_t status;

    status = __SocketRequestBegin(message, handle, &resource);
    if (status == OS_EOK) {
        status = NetworkManagerSocketAccept(message, resource);
        mtx_unlock(&g_socketExecution);
    }
    if (status != OS_EOK) {
        sys_socket_accept_response(message, status, NULL, 0,
            &(struct sys_grant_token){ 0 }, &(struct sys_socket_info){ 0 });
    }
}

static oserr_t
NetworkManagerSocketListenLocked(
        _In_ uuid_t Handle,
        _In_ int    ConnectionCount)
{
    Socket_t* Socket;
    
    TRACE("[net_manager] [listen] %u, %i", Handle, ConnectionCount);
    
    Socket = NetworkManagerSocketGetUnsafe(Handle);
    if (!Socket) {
        ERROR("[net_manager] [listen] invalid handle %u", Handle);
        return OS_ENOENT;
    }
    
    if (Socket->Configuration.Connecting || Socket->Configuration.Connected) {
        return OS_EINPROGRESS;
    }
    return SocketListenImpl(Socket, ConnectionCount);
}

oserr_t
NetworkManagerSocketListen(
        _In_ uuid_t Handle,
        _In_ int    ConnectionCount)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketListenLocked(Handle, ConnectionCount);
    mtx_unlock(&g_socketExecution);
    return status;
}

void sys_socket_listen_invocation(struct gracht_message* message, const uuid_t handle, const int backlog)
{
    uuid_t  resource;
    oserr_t status;

    status = __SocketRequestBegin(message, handle, &resource);
    if (status == OS_EOK) {
        status = NetworkManagerSocketListen(resource, backlog);
        mtx_unlock(&g_socketExecution);
    }
    sys_socket_listen_response(message, status);
}

static oserr_t
NetworkManagerSocketPairLocked(
        _In_ uuid_t Handle1,
        _In_ uuid_t Handle2)
{
    Socket_t*  Socket1;
    Socket_t*  Socket2;
    oserr_t Status;
    
    TRACE("[net_manager] [pair] %u, %u", Handle1, Handle2);
    
    Socket1 = NetworkManagerSocketGetUnsafe(Handle1);
    Socket2 = NetworkManagerSocketGetUnsafe(Handle2);
    if (!Socket1 || !Socket2) {
        if (!Socket1) {
            ERROR("[net_manager] [pair] invalid handle1 %u", Handle1);
        }
        
        if (!Socket2) {
            ERROR("[net_manager] [pair] invalid handle2 %u", Handle2);
        }
        return OS_ENOENT;
    }
    
    if (Socket1->Configuration.Passive || Socket2->Configuration.Passive) {
        ERROR("[net_manager] [pair] either Socket1/2 was marked passive (%u, %u)", 
            Socket1->Configuration.Passive, Socket2->Configuration.Passive);
        return OS_ENOTSUPPORTED;
    }
    
    if (Socket1->Configuration.Connected || Socket2->Configuration.Connected) {
        ERROR("[net_manager] [pair] either Socket1/2 was marked connected (%u, %u)", 
            Socket1->Configuration.Connected, Socket2->Configuration.Connected);
        return OS_EISCONNECTED;
    }
    
    Status = DomainPair(Socket1, Socket2);
    if (Status == OS_EOK) {
        Socket1->Configuration.Connecting = 0;
        Socket1->Configuration.Connected  = 1;
        
        Socket2->Configuration.Connecting = 0;
        Socket2->Configuration.Connected  = 1;
    }
    return Status;
}

oserr_t
NetworkManagerSocketPair(
        _In_ uuid_t Handle1,
        _In_ uuid_t Handle2)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketPairLocked(Handle1, Handle2);
    mtx_unlock(&g_socketExecution);
    return status;
}

void sys_socket_pair_invocation(struct gracht_message* message, const uuid_t handle1, const uuid_t handle2)
{
    uuid_t   resource1;
    uuid_t   resource2;
    uint64_t group;
    oserr_t  status;

    status = __SocketRequestBegin(message, handle1, &resource1);
    if (status == OS_EOK) {
        __SocketCaller(message, &group);
        
        status = OSHandleValidate(
            &g_socketRegistration,
            group,
            handle2,
            OS_SOCKET_ACCESS,
            &resource2
        );
        if (status == OS_EOK) {
            status = NetworkManagerSocketPair(resource1, resource2);
        }
        mtx_unlock(&g_socketExecution);
    }
    sys_socket_pair_response(message, status);
}

static oserr_t
NetworkManagerSocketSetOptionLocked(
        _In_ uuid_t           Handle,
        _In_ int              Protocol,
        _In_ unsigned int     Option,
        _In_ const void*      Data,
        _In_ socklen_t        DataLength)
{
    Socket_t* Socket;
    
    Socket = NetworkManagerSocketGetUnsafe(Handle);
    if (!Socket) {
        return OS_ENOENT;
    }
    return SetSocketOptionImpl(Socket, Protocol, Option, Data, DataLength);
}

oserr_t
NetworkManagerSocketSetOption(
        _In_ uuid_t           Handle,
        _In_ int              Protocol,
        _In_ unsigned int     Option,
        _In_ const void*      Data,
        _In_ socklen_t        DataLength)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketSetOptionLocked(Handle, Protocol, Option, Data, DataLength);
    mtx_unlock(&g_socketExecution);
    return status;
}

void sys_socket_set_option_invocation(struct gracht_message* message, const uuid_t handle, const int protocol,
                                      const unsigned int option, const uint8_t* data, const uint32_t data_count, const int length)
{
    uuid_t  resource;
    oserr_t status;

    status = __SocketRequestBegin(message, handle, &resource);
    if (status == OS_EOK) {
        if (length < 0 || (uint32_t)length != data_count) {
            status = OS_EINVALPARAMS;
        } else {
            status = NetworkManagerSocketSetOption(
                resource,
                protocol,
                option,
                data,
                length
            );
        }
        mtx_unlock(&g_socketExecution);
    }
    sys_socket_set_option_response(message, status);
}

static oserr_t
NetworkManagerSocketGetOptionLocked(
        _In_  uuid_t           Handle,
        _In_  int              Protocol,
        _In_  unsigned int     Option,
        _In_  void*            Data,
        _Out_ socklen_t*       DataLengthOut)
{
    Socket_t* Socket;
    
    Socket = NetworkManagerSocketGetUnsafe(Handle);
    if (!Socket) {
        return OS_ENOENT;
    }
    return GetSocketOptionImpl(Socket, Protocol, Option, Data, DataLengthOut);
}

oserr_t
NetworkManagerSocketGetOption(
        _In_  uuid_t           Handle,
        _In_  int              Protocol,
        _In_  unsigned int     Option,
        _In_  void*            Data,
        _Out_ socklen_t*       DataLengthOut)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketGetOptionLocked(Handle, Protocol, Option, Data, DataLengthOut);
    mtx_unlock(&g_socketExecution);
    return status;
}

void sys_socket_get_option_invocation(struct gracht_message* message, const uuid_t handle,
        const int protocol, const unsigned int option)
{
    char      buffer[32] = { 0 };
    socklen_t length = 0;
    uuid_t    resource;
    oserr_t   status;

    status = __SocketRequestBegin(message, handle, &resource);
    if (status == OS_EOK) {
        status = NetworkManagerSocketGetOption(
            resource,
            protocol,
            option,
            buffer,
            &length
        );
        mtx_unlock(&g_socketExecution);
    }
    if (status != OS_EOK || length > sizeof(buffer)) {
        if (status == OS_EOK) {
            status = OS_EINVALPARAMS;
        }
        length = 0;
    }
    sys_socket_get_option_response(message, status, (uint8_t*)buffer, length, (int)length);
}

static oserr_t
NetworkManagerSocketGetAddressLocked(
        _In_ uuid_t           Handle,
        _In_ int              Source,
        _In_ struct sockaddr* Address)
{
    Socket_t* Socket;
    
    Socket = NetworkManagerSocketGetUnsafe(Handle);
    if (!Socket) {
        return OS_ENOENT;
    }
    return DomainGetAddress(Socket, Source, Address);
}

oserr_t
NetworkManagerSocketGetAddress(
        _In_ uuid_t           Handle,
        _In_ int              Source,
        _In_ struct sockaddr* Address)
{
    oserr_t status;

    if (mtx_lock(&g_socketExecution) != thrd_success) {
        return OS_EUNKNOWN;
    }
    status = NetworkManagerSocketGetAddressLocked(Handle, Source, Address);
    mtx_unlock(&g_socketExecution);
    return status;
}

void sys_socket_get_address_invocation(struct gracht_message* message,
                                       const uuid_t handle, const enum sys_address_type type)
{
    struct sockaddr_storage address = { 0 };
    uuid_t                  resource;
    oserr_t                 status;

    status = __SocketRequestBegin(message, handle, &resource);
    if (status == OS_EOK) {
        status = NetworkManagerSocketGetAddress(
            resource,
            type,
            (struct sockaddr*)&address
        );
        mtx_unlock(&g_socketExecution);
    }
    sys_socket_get_address_response(
        message,
        status,
        (uint8_t*)&address,
        status == OS_EOK ? address.__ss_len : 0
    );
}

Socket_t*
NetworkManagerSocketGetUnsafe(
        _In_ uuid_t Handle)
{
    return (Socket_t*)rb_tree_lookup_value(&g_sockets, (void*)(uintptr_t)Handle);
}
