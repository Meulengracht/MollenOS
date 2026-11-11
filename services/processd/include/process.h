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
 */

#ifndef __PROCESS_INTERFACE__
#define __PROCESS_INTERFACE__

#include <ds/mstring.h>
#include <gracht/server.h>
#include <ddk/types/capabilities.h>
#include <internal/_service.h>
#include <os/services/process.h>
#include <os/types/handle.h>
#include <os/types/shm.h>
#include <os/types/grants.h>
#include <os/usched/cond.h>
#include <os/usched/mutex.h>
#include <time.h>
#include <pe.h>

// forward declarations
struct sys_process_inheritance_input;

enum ProcessState {
    ProcessState_RUNNING,
    ProcessState_TERMINATING
};

struct ProcessOptions {
    uuid_t         Scope;
    UInteger64_t   MemoryLimit;
    Capabilities_t Capabilities;
    const char*    WorkingDirectory;
    uint32_t       InheritationBlockLength;
    uint32_t       EnvironmentBlockLength;
    const void*    DataBuffer;
};

struct ProcessStartupData {
    void*    Buffer;
    uint32_t Length;
};

typedef struct Process {
    OSHandle_t                 handle;
    uuid_t                     primary_thread_id;
    Capabilities_t             capabilities;
    clock_t                    tick_base;
    enum ProcessState          state;
    struct usched_mtx          mutex;
    struct usched_cnd          condition;
    int                        references;
    struct PEImageLoadContext* load_context;
    int                        exit_code;
    struct ProcessStartupData  startup_data;

    mstring_t* name;
    mstring_t* working_directory;
    mstring_t* assembly_directory;
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
 * @brief
 *
 * @param path
 * @param bufferOut Receives the allocated file contents. NULL checks existence
 *                  without allocating or reading the contents.
 * @param bufferSizeOut Optionally receives the file size.
 * @return
 */
extern oserr_t
PmBootstrapFindRamdiskFile(
    _In_      mstring_t* path,
    _Out_Opt_ void**     bufferOut,
    _Out_Opt_ size_t*    bufferSizeOut);

/**
 * @brief Authorize the requested initial capabilities before preparing a child.
 * @param caller Authenticated request context, or NULL for an unauthenticated transport.
 * @param capabilities Requested child capability mask; zero needs no grant authority.
 * @return OS_EOK, OS_EINVALPARAMS for unknown bits, or OS_EPERMISSIONS.
 * NULL never grants authority. Service bootstrap uses a separate entry point.
 */
extern oserr_t
PmAuthorizeSpawn(
    _In_ const struct gracht_security_context* caller,
    _In_ Capabilities_t                        capabilities);

/**
 * @brief Spawns a new process with the given configuration. The process assumes a valid PE image
 * and builds all required tables for the new process.
 *
 * @param[In] caller Transport-authenticated send-time credentials. NULL carries no authority.
 * @param[In] path
 * @param[In] args
 * @param[In] procOpts
 * @param[Out] handleOut
 * @return
 */
extern oserr_t
PmCreateProcess(
        _In_  const struct gracht_security_context* caller,
        _In_  const char*            path,
        _In_  const char*            args,
        _In_  struct ProcessOptions* procOpts,
        _Out_ uuid_t*                handleOut);

/**
 * @brief Create a service from processd's trusted boot service configuration.
 * Never call this entry point from an IPC handler. The kernel still checks
 * processd's grant capability. No absent or fabricated caller context is used.
 * @param path Service image path.
 * @param args Service arguments.
 * @param procOpts Configuration obtained by internal service discovery.
 * @param handleOut Receives the created process handle.
 * @return Creation status.
 */
oserr_t
PmCreateServiceProcess(
    _In_  const char*            path,
    _In_  const char*            args,
    _In_  struct ProcessOptions* procOpts,
    _Out_ uuid_t*                handleOut);

/**
 * @brief Copies startup data to a token-authorized destination.
 * The decoded transfer must have valid selector fields. The authenticated caller
 * must belong to the selected process's security group before the token is imported.
 *
 * @param caller Transport-authenticated request context.
 * @param threadHandle Primary thread identifying the process.
 * @param destination Validated transfer description.
 * @param processHandleOut Receives the process handle on success.
 * @param bytesWrittenOut Receives the complete payload length on success.
 * @return Transfer or authorization status.
 */
extern oserr_t
PmGetProcessStartupInformation(
    _In_  const struct gracht_security_context* caller,
    _In_  uuid_t                                threadHandle,
    _In_  const SHMTransfer_t*                   destination,
    _Out_ uuid_t*                               processHandleOut,
    _Out_ uint32_t*                             bytesWrittenOut);

/**
 * @brief Builds the immutable startup payload before publishing the process.
 * Call once after loading initial modules and validating the private input.
 *
 * @param process Unpublished process to prepare.
 * @param environment Validated environment block. May be NULL when environmentLength is 0.
 * @param environmentLength Environment block bytes, including the final empty string.
 * @param arguments Complete argument string.
 * @param argumentsLength Argument bytes, including the terminator.
 * @param input Decoded and contract-validated inheritance. Not consumed.
 * @return OS_EOK, or a size, allocation, module-list or import error.
 */
extern oserr_t
PmPrepareStartupInformation(
    _In_ Process_t*                                 process,
    _In_ const char*                                environment,
    _In_ size_t                                     environmentLength,
    _In_ const char*                                arguments,
    _In_ size_t                                     argumentsLength,
    _In_ const struct sys_process_inheritance_input* input);

/**
 * @brief Finds a process by its primary thread and adds a reference under the registry lock.
 * This lookup does not authorize the caller. Release a successful result with PmReleaseProcess.
 * @param threadHandle Primary thread identifying the process.
 * @param processOut Receives the referenced process.
 * @return OS_EOK, OS_ENOENT or OS_EOVERFLOW.
 */
extern oserr_t
PmReferenceProcessByThread(
    _In_  uuid_t      threadHandle,
    _Out_ Process_t** processOut);

/**
 * @brief Releases a process reference and destroys the process after the last release.
 * The caller must not hold the process mutex.
 * @param process Referenced process.
 */
extern void
PmReleaseProcess(
    _In_ Process_t* process);

/**
 * @brief Retrieves the process associated by the handle.
 *
 * @param[In] handle
 * @return Returns NULL if the handle is invalid.
 */
extern Process_t*
PmGetProcessByHandle(
        _In_ uuid_t handle);

#endif //!__PROCESS_INTERFACE__
