/**
 * Copyright, Philip Meulengracht
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

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <os/usched/cond.h>
#include <os/usched/job.h>
#include <sys_usb_service_server.h>

#include "manager.h"

enum __TopologyOperation {
    TOPOLOGY_REGISTER_CONTROLLER,
    TOPOLOGY_UNREGISTER_CONTROLLER,
    TOPOLOGY_REGISTER_HUB,
    TOPOLOGY_UNREGISTER_HUB,
    TOPOLOGY_PORT_EVENT,
    TOPOLOGY_PORT_ERROR,
    TOPOLOGY_CONTROLLER_COUNT,
    TOPOLOGY_CONTROLLER_QUERY
};

/**
 * @brief RPC arguments and completion state for a topology operation. The handler
 * keeps the command on its stack until the worker has finished using it.
 */
struct __TopologyCommand {
    struct __TopologyCommand* Next;
    struct usched_cnd         Completion;
    bool                      Done;
    enum __TopologyOperation  Operation;
    struct gracht_message*    Message;
    union {
        struct {
            uuid_t                   DriverId;
            const struct sys_device* Device;
            int                      Kind;
            int                      PortCount;
        } Controller;
        struct {
            uuid_t   ParentId;
            uuid_t   DeviceId;
            uuid_t   DriverId;
            int      PortCount;
            uint16_t Characteristics;
        } Hub;
        struct {
            uuid_t  DeviceId;
            uint8_t PortAddress;
        } Target;
        int Index;
    } Data;
};

/**
 * @brief Pending topology commands, worker job and synchronization state. The
 * mutex protects the queue and command completion while handlers wait.
 */
struct __TopologyQueue {
    struct usched_mtx         Mutex;
    struct usched_cnd         Ready;
    struct __TopologyCommand* Head;
    struct __TopologyCommand* Tail;
    uuid_t                    Job;
    bool                      Initialized;
    bool                      Running;
};

static struct __TopologyQueue g_topology;

static void
__ExecuteCommand(
    _In_ struct __TopologyCommand* command)
{
    USBControllerDevice_t snapshot;
    Device_t*             device;
    oserr_t               status;

    // Called only by the worker, with the queue lock released. Registry access
    // is serialized here, but enumeration and teardown can wait for RPC replies
    // processed by the receive job.
    switch (command->Operation) {
        case TOPOLOGY_REGISTER_CONTROLLER:
            device = from_sys_device(command->Data.Controller.Device);
            if (device == NULL) {
                ERROR("USB: cannot convert controller registration");
                break;
            }

            status = UsbCoreControllerRegister(
                command->Data.Controller.DriverId,
                device,
                command->Data.Controller.Kind,
                command->Data.Controller.PortCount
            );
            if (status != OS_EOK) {
                ERROR("USB: controller registration failed (%u)", status);
            }
            break;
        case TOPOLOGY_UNREGISTER_CONTROLLER:
            status = UsbCoreControllerUnregister(command->Data.Target.DeviceId);
            if (status != OS_EOK) {
                WARNING("USB: controller %u removal failed (%u)", command->Data.Target.DeviceId, status);
            }
            break;
        case TOPOLOGY_REGISTER_HUB:
            UsbCoreHubsRegisterAndConfigure(
                command->Data.Hub.ParentId,
                command->Data.Hub.DeviceId,
                command->Data.Hub.DriverId,
                command->Data.Hub.PortCount,
                command->Data.Hub.Characteristics
            );
            break;
        case TOPOLOGY_UNREGISTER_HUB:
            UsbCoreHubsUnregister(command->Data.Target.DeviceId);
            break;
        case TOPOLOGY_PORT_EVENT:
            UsbCorePortEvent(command->Data.Target.DeviceId, command->Data.Target.PortAddress);
            break;
        case TOPOLOGY_PORT_ERROR:
            UsbCorePortError(command->Data.Target.DeviceId, command->Data.Target.PortAddress);
            break;
        case TOPOLOGY_CONTROLLER_COUNT:
            sys_usb_get_controller_count_response(command->Message, UsbCoreControllerCount());
            break;
        case TOPOLOGY_CONTROLLER_QUERY:
            UsbCoreControllerSnapshot(command->Data.Index, &snapshot);
            sys_usb_get_controller_response(command->Message, (uint8_t*)&snapshot, sizeof(snapshot));
            break;
    }
}

static void
__TopologyWorker(
    _In_ void* argument,
    _In_ void* cancellationToken)
{
    struct __TopologyCommand* command;
    _CRT_UNUSED(argument);
    _CRT_UNUSED(cancellationToken);

    usched_mtx_lock(&g_topology.Mutex);
    for (;;) {
        while (g_topology.Head == NULL && g_topology.Running) {
            usched_cnd_wait(&g_topology.Ready, &g_topology.Mutex);
        }

        // An empty queue here means shutdown was requested and all commands ran.
        if (g_topology.Head == NULL) {
            break;
        }
        
        command = g_topology.Head;
        g_topology.Head = command->Next;
        if (g_topology.Head == NULL) {
            g_topology.Tail = NULL;
        }
        usched_mtx_unlock(&g_topology.Mutex);

        __ExecuteCommand(command);

        // Set Done under the lock used by the waiting handler. Once the lock is
        // released, the handler can return and its command is no longer valid.
        usched_mtx_lock(&g_topology.Mutex);
        command->Done = true;
        usched_cnd_notify_one(&command->Completion);
    }
    usched_mtx_unlock(&g_topology.Mutex);
}

oserr_t
UsbTopologyStart(void)
{
    // The service must serialize start and stop calls. Keep the mutex and
    // condition variable after joining the worker because completed RPC handlers
    // may still be waking up.
    if (!g_topology.Initialized) {
        usched_mtx_init(&g_topology.Mutex, USCHED_MUTEX_PLAIN);
        usched_cnd_init(&g_topology.Ready);
        g_topology.Initialized = true;
    }

    usched_mtx_lock(&g_topology.Mutex);
    if (g_topology.Job != UUID_INVALID) {
        usched_mtx_unlock(&g_topology.Mutex);
        return OS_EBUSY;
    }
    
    g_topology.Running = true;
    g_topology.Job = usched_job_queue(__TopologyWorker, NULL);
    if (g_topology.Job == UUID_INVALID) {
        g_topology.Running = false;
        usched_mtx_unlock(&g_topology.Mutex);
        ERROR("USB: cannot start topology worker");
        return OS_EOOM;
    }
    usched_mtx_unlock(&g_topology.Mutex);
    
    TRACE("USB: topology worker started");
    return OS_EOK;
}

oserr_t
UsbTopologyStop(void)
{
    uuid_t job;
    int    exitCode;

    if (!g_topology.Initialized) {
        return OS_EOK;
    }

    usched_mtx_lock(&g_topology.Mutex);
    
    job = g_topology.Job;
    if (job == UUID_INVALID) {
        usched_mtx_unlock(&g_topology.Mutex);
        return OS_EOK;
    }
    
    if (job == usched_job_current()) {
        usched_mtx_unlock(&g_topology.Mutex);
        return OS_EBUSY;
    }
    
    g_topology.Running = false;
    usched_cnd_notify_one(&g_topology.Ready);
    usched_mtx_unlock(&g_topology.Mutex);

    // Release the queue lock before joining so the worker can finish queued
    // commands and exit. If joining fails, keep the job ID so stop can be retried
    // before the service frees state the worker may still use.
    if (usched_job_join(job, &exitCode) != 0) {
        WARNING("USB: topology worker join failed; cleanup deferred");
        return OS_EUNKNOWN;
    }
    
    usched_mtx_lock(&g_topology.Mutex);
    g_topology.Job = UUID_INVALID;
    usched_mtx_unlock(&g_topology.Mutex);
    
    TRACE("USB: topology worker stopped");
    return OS_EOK;
}

static bool
__SubmitCommand(
    _InOut_ struct __TopologyCommand* command)
{
    usched_cnd_init(&command->Completion);
    usched_mtx_lock(&g_topology.Mutex);
    
    if (!g_topology.Running) {
        usched_mtx_unlock(&g_topology.Mutex);
        WARNING("USB: topology command %u rejected during shutdown", command->Operation);
        return false;
    }

    if (g_topology.Tail == NULL) {
        g_topology.Head = command;
    } else {
        g_topology.Tail->Next = command;
    }
    
    g_topology.Tail = command;
    usched_cnd_notify_one(&g_topology.Ready);

    // Gracht continues receiving messages and replies while this handler waits.
    // The handler must wait for completion even during shutdown or cancellation.
    // The worker still uses the stack command and, for registration and queries,
    // the RPC message data.
    while (!command->Done) {
        usched_cnd_wait(&command->Completion, &g_topology.Mutex);
    }
    usched_mtx_unlock(&g_topology.Mutex);
    return true;
}

void
sys_usb_register_controller_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           driverId,
    _In_ const struct sys_device* device,
    _In_ const int              type,
    _In_ const int              portCount)
{
    struct __TopologyCommand command = {
        .Operation = TOPOLOGY_REGISTER_CONTROLLER,
        .Data.Controller = { driverId, device, type, portCount }
    };
    _CRT_UNUSED(message);

    // Wait here so the decoded device stays valid until the worker converts it.
    (void)__SubmitCommand(&command);
}

void
sys_usb_unregister_controller_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId)
{
    struct __TopologyCommand command = {
        .Operation = TOPOLOGY_UNREGISTER_CONTROLLER, .Data.Target.DeviceId = deviceId
    };
    _CRT_UNUSED(message);
    (void)__SubmitCommand(&command);
}

void
sys_usb_register_hub_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           parentHubDeviceId,
    _In_ const uuid_t           deviceId,
    _In_ const uuid_t           driverId,
    _In_ const int              portCount,
    _In_ const uint16_t         characteristics)
{
    struct __TopologyCommand command = {
        .Operation = TOPOLOGY_REGISTER_HUB,
        .Data.Hub = { parentHubDeviceId, deviceId, driverId, portCount, characteristics }
    };
    _CRT_UNUSED(message);
    (void)__SubmitCommand(&command);
}

void
sys_usb_unregister_hub_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId)
{
    struct __TopologyCommand command = {
        .Operation = TOPOLOGY_UNREGISTER_HUB, .Data.Target.DeviceId = deviceId
    };
    _CRT_UNUSED(message);
    (void)__SubmitCommand(&command);
}

void
sys_usb_port_event_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           hubDeviceId,
    _In_ const uint8_t          portAddress)
{
    struct __TopologyCommand command = {
        .Operation = TOPOLOGY_PORT_EVENT, .Data.Target = { hubDeviceId, portAddress }
    };
    _CRT_UNUSED(message);
    (void)__SubmitCommand(&command);
}

void
sys_usb_port_error_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           hubDeviceId,
    _In_ const uint8_t          portAddress)
{
    struct __TopologyCommand command = {
        .Operation = TOPOLOGY_PORT_ERROR, .Data.Target = { hubDeviceId, portAddress }
    };
    _CRT_UNUSED(message);
    (void)__SubmitCommand(&command);
}

void
sys_usb_get_controller_count_invocation(
    _In_ struct gracht_message* message)
{
    struct __TopologyCommand command = {
        .Operation = TOPOLOGY_CONTROLLER_COUNT, .Message = message
    };

    // The response has no error field. If the worker has stopped accepting
    // commands, report an empty registry so the client still gets a reply.
    if (!__SubmitCommand(&command)) {
        sys_usb_get_controller_count_response(message, 0);
    }
}

void
sys_usb_get_controller_invocation(
    _In_ struct gracht_message* message,
    _In_ const int             index)
{
    struct __TopologyCommand command = {
        .Operation = TOPOLOGY_CONTROLLER_QUERY, .Message = message, .Data.Index = index
    };
    USBControllerDevice_t empty = { 0 };

    if (!__SubmitCommand(&command)) {
        sys_usb_get_controller_response(message, (uint8_t*)&empty, sizeof(empty));
    }
}
