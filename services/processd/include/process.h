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
#include <os/services/process.h>
#include <os/types/handle.h>
#include <os/types/shm.h>
#include <os/usched/cond.h>
#include <os/usched/mutex.h>
#include <time.h>
#include <pe.h>

enum ProcessState {
    ProcessState_RUNNING,
    ProcessState_TERMINATING
};

struct ProcessOptions {
    uuid_t       Scope;
    UInteger64_t MemoryLimit;
    const char*  WorkingDirectory;
    uint32_t     InheritationBlockLength;
    uint32_t     EnvironmentBlockLength;
    OSHandle_t   DataBufferHandle;
    // DataBuffer is set if it should used instead of the
    // DataBufferHandle. This is needed when doing internal
    // spawning.
    const void* DataBuffer;
};

typedef struct Process {
    OSHandle_t                 handle;
    uuid_t                     primary_thread_id;
    clock_t                    tick_base;
    enum ProcessState          state;
    struct usched_mtx          mutex;
    struct usched_cnd          condition;
    int                        references;
    struct PEImageLoadContext* load_context;
    int                        exit_code;

    mstring_t* name;
    mstring_t* working_directory;
    mstring_t* assembly_directory;

    const char* arguments;
    size_t      arguments_length;
    void*       inheritation_block;
    size_t      inheritation_block_length;
    char*       environment_block;
    size_t      environment_block_length;
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
 * @param bufferOut
 * @param bufferSizeOut
 * @return
 */
extern oserr_t
PmBootstrapFindRamdiskFile(
        _In_  mstring_t* path,
        _Out_ void**     bufferOut,
        _Out_ size_t*    bufferSizeOut);

/**
 * @brief Spawns a new process with the given configuration. The process assumes a valid PE image
 * and builds all required tables for the new process.
 *
 * @param[In] path
 * @param[In] args
 * @param[In] procOpts
 * @param[Out] handleOut
 * @return
 */
extern oserr_t
PmCreateProcess(
        _In_  const char*            path,
        _In_  const char*            args,
        _In_  struct ProcessOptions* procOpts,
        _Out_ uuid_t*                handleOut);

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
