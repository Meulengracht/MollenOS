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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Process Manager
 * - Contains the implementation of the process-manager which keeps track
 *   of running applications.
 */

#ifndef __PROCESS_INTERFACE__
#define __PROCESS_INTERFACE__

#include <ds/list.h>
#include <gracht/server.h>
#include <os/osdefs.h>
#include <os/process.h>
#include <os/usched/mutex.h>
#include <time.h>
#include "requests.h"

// Forward declarations
DECL_STRUCT(PeExecutable);
DECL_STRUCT(MString);

enum ProcessState {
    ProcessState_RUNNING,
    ProcessState_TERMINATING
};

typedef struct Process {
    UUId_t                 handle;
    UUId_t                 primary_thread_id;
    clock_t                tick_base;
    enum ProcessState      state;
    struct usched_mtx      lock;
    list_t                 requests;
    PeExecutable_t*        image;
    ProcessConfiguration_t config;
    int                    exit_code;

    MString_t* name;
    MString_t* path;
    MString_t* working_directory;
    MString_t* assembly_directory;

    const char* arguments;
    size_t      arguments_length;
    void*       inheritation_block;
    size_t      inheritation_block_length;
} Process_t;

/**
 * @brief Initializes the subsystems for managing the running processes.
 */
extern void PmInitialize(void);

/**
 * @brief Initializes the debugger functionality in the process manager.
 */
extern void PmDebuggerInitialize(void);

/**
 * @brief Bootstraps the entire system by parsing ramdisk for system services.
 */
extern void PmBootstrap(void);

/**
 * @brief Cleans up any resources related to bootstrapping. This includes the mapped
 * ramdisk, which should only be cleaned up on process exit.
 */
extern void PmBootstrapCleanup(void);

/**
 * @brief
 *
 * @param path
 * @param bufferOut
 * @param bufferSizeOut
 * @return
 */
extern oscode_t
PmBootstrapFindRamdiskFile(
        _In_  MString_t* path,
        _Out_ void**     bufferOut,
        _Out_ size_t*    bufferSizeOut);

/**
 * @brief Registers a request to the process. As long as a request is registered to a process the process
 * termination will not complete before all requests has been handled.
 *
 * @param[In] handle  A handle for the process to register the request for
 * @param[In] request The request that should be registered.
 * @return
 */
extern Process_t*
RegisterProcessRequest(
        _In_ UUId_t     handle,
        _In_ Request_t* request);

/**
 * @brief Unregisters a registered request, if the process is terminating and the number of requests reach 0,
 * then process destruction will also occur.
 *
 * @param[In] process A process instance that the request is registered too
 * @param[In] request The request that should be unregistered.
 */
extern void
UnregisterProcessRequest(
        _In_ Process_t* process,
        _In_ Request_t* request);

/**
 * @brief Spawns a new process with the given configuration. The process assumes a valid PE image
 * and builds all required tables for the new process.
 *
 * @param[In] path
 * @param[In] args
 * @param[In] inherit
 * @param[In] processConfiguration
 * @param[In] cancellationToken
 * @param[Out] handleOut
 * @return
 */
extern oscode_t
PmCreateProcessInternal(
        _In_  const char*             path,
        _In_  const char*             args,
        _In_  const void*             inherit,
        _In_  ProcessConfiguration_t* processConfiguration,
        _In_  void*                   cancellationToken,
        _Out_ UUId_t*                 handleOut);

/**
 * @brief Retrieves the process associated by the handle.
 *
 * @param[In] handle
 * @return Returns NULL if the handle is invalid.
 */
extern Process_t*
PmGetProcessByHandle(
        _In_ UUId_t handle);

#endif //!__PROCESS_INTERFACE__
