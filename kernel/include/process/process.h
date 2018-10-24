/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Alias & Process Management
 * - The implementation of phoenix is responsible for managing alias's, handle
 *   file events and creating/destroying processes.
 */

#ifndef __VALI_PROCESS_H__
#define __VALI_PROCESS_H__

#include <os/osdefs.h>
#include <os/process.h>
#include <ds/collection.h>
#include <time.h>

typedef struct _SystemMemorySpace SystemMemorySpace_t;
typedef struct _MCorePeFile MCorePeFile_t;
typedef struct _BlockBitmap BlockBitmap_t;
typedef struct _MString MString_t;

#define PROCESS_INITIAL_STACK   0x1000
#define PROCESS_MAX_STACK       (4 << 20)

// Types of processes that can be created.
typedef enum _SystemProcessType {
    ProcessNormal,
    ProcessService
} SystemProcessType_t;

// System process structure
// Data container that encompass a range of threads and shared resources
typedef struct _SystemProcess {
    SystemProcessType_t         Type;
    MString_t*                  Name;
    MString_t*                  Path;

    ProcessStartupInformation_t StartupInformation;
    MString_t*                  WorkingDirectory;
    MString_t*                  BaseDirectory;
    UUId_t                      MainThreadId;
    clock_t                     StartedAt;
    
    // Resources
    Collection_t*               Pipes;          // Move to handles
    Collection_t*               FileMappings;   // Move to handles
    SystemMemorySpace_t*        MemorySpace;    // just a reference
    BlockBitmap_t*              Heap;           // Move to memory-space
    uintptr_t                   SignalHandler;  // Move to memory space/store in thread

    // Image resources
    MCorePeFile_t*              Executable;
    uintptr_t                   NextLoadingAddress;
    int                         Code;
} SystemProcess_t;

/* CreateProcess 
 * Creates a new handle, allocates a new handle and initializes a thread that performs
 * the rest of setup required. */
KERNELAPI OsStatus_t KERNELABI
CreateProcess(
    _In_  MString_t*                    Path,
    _In_  ProcessStartupInformation_t*  StartupInformation,
    _In_  SystemProcessType_t           Type,
    _Out_ UUId_t*                       Handle);

/* DestroyProcess
 * Callback invoked by the handle system when references on a process reaches zero */
KERNELAPI OsStatus_t KERNELABI
DestroyProcess(
    _In_ void*                          Resource);

/* GetProcess
 * This function retrieves the process structure by it's handle. */
KERNELAPI SystemProcess_t* KERNELABI
GetProcess(
    _In_ UUId_t Handle);

/* GetCurrentProcess
 * Retrieves the currently running process, identified by its thread. If none NULL is returned. */
KERNELAPI SystemProcess_t* KERNELABI
GetCurrentProcess(void);

/* GetWorkingDirectory
 * Retrieves the process's working directory path determined by the handle. */
KERNELAPI MString_t* KERNELABI
GetWorkingDirectory(
    _In_ UUId_t Handle);

/* GetBaseDirectory
 * Retrieves the process's working base path determined by the handle. */
KERNELAPI MString_t* KERNELABI
GetBaseDirectory(
    _In_ UUId_t Handle);

#endif //!__VALI_PROCESS_H__
