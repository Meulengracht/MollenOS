/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include "../../librt/libds/pe/pe.h"
#include <modules/manager.h>
#include <system/utils.h>
#include <os/process.h>
#include <ds/mstring.h>
#include <threading.h>
#include <scheduler.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>

OsStatus_t
ScModuleGetStartupInformation(
    _In_ void*   InheritanceBlock, 
    _In_ size_t* InheritanceBlockLength,
    _In_ void*   ArgumentBlock,
    _In_ size_t* ArgumentBlockLength)
{
    SystemModule_t* Module = GetCurrentModule();
    if (Module == NULL) {
        return OsInvalidPermissions;
    }

    if (Module->ArgumentBlock != NULL) {
        if (ArgumentBlock != NULL) {
            memcpy((void*)ArgumentBlock, Module->ArgumentBlock,
                MIN(*ArgumentBlockLength, Module->ArgumentBlockLength));
            *ArgumentBlockLength = MIN(*ArgumentBlockLength, Module->ArgumentBlockLength);
        }
        else if (ArgumentBlockLength != NULL) {
            *ArgumentBlockLength = Module->ArgumentBlockLength;
        }
    }
    else if (ArgumentBlockLength != NULL) {
        *ArgumentBlockLength = 0;
    }

    if (Module->InheritanceBlock != NULL) {
        if (InheritanceBlock != NULL) {
            size_t BytesToCopy = MIN(*InheritanceBlockLength, Module->InheritanceBlockLength);
            memcpy((void*)InheritanceBlock, Module->InheritanceBlock, BytesToCopy);
            *InheritanceBlockLength = BytesToCopy;
        }
        else if (InheritanceBlockLength != NULL) {
            *InheritanceBlockLength = Module->InheritanceBlockLength;
        }
    }
    else if (InheritanceBlockLength != NULL) {
        *InheritanceBlockLength = 0;
    }
    return OsSuccess;
}

OsStatus_t 
ScModuleGetCurrentId(
    _In_ UUId_t* Handle)
{
    SystemModule_t* Module = GetCurrentModule();
    if (Module != NULL) {
        *Handle = Module->Handle;
        return OsSuccess;
    }
    return OsInvalidPermissions;
}

OsStatus_t
ScModuleGetCurrentName(
    _In_ const char* Buffer,
    _In_ size_t      MaxLength)
{
    SystemModule_t* Module = GetCurrentModule();
    if (Module == NULL) {
        return OsInvalidPermissions;
    }
    memset((void*)Buffer, 0, MaxLength);
    memcpy((void*)Buffer, MStringRaw(Module->Executable->Name), MIN(MStringSize(Module->Executable->Name) + 1, MaxLength));
    return OsSuccess;
}

OsStatus_t
ScModuleGetModuleHandles(
    _In_ Handle_t ModuleList[PROCESS_MAXMODULES])
{
    SystemModule_t* Module = GetCurrentModule();
    if (Module == NULL) {
        return OsError;
    }
    return PeGetModuleHandles(Module->Executable, ModuleList);
}

OsStatus_t
ScModuleGetModuleEntryPoints(
    _In_ Handle_t ModuleList[PROCESS_MAXMODULES])
{
    SystemModule_t* Module = GetCurrentModule();
    if (Module == NULL) {
        return OsError;
    }
    return PeGetModuleEntryPoints(Module->Executable, ModuleList);
}

OsStatus_t
ScModuleExit(
    _In_ int ExitCode)
{
    MCoreThread_t*  Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    SystemModule_t* Module = GetCurrentModule();
    OsStatus_t      Status = OsError;
    if (Module != NULL) {
        WARNING("Process %s terminated with code %" PRIiIN "", MStringRaw(Module->Executable->Name), ExitCode);
        // Are we detached? Then call only thread cleanup
        if (Thread->ParentThreadId == UUID_INVALID) {
            Status = TerminateThread(Thread->Id, ExitCode, 1);
        }
        else {
            Status = TerminateThread(Module->PrimaryThreadId, ExitCode, 1);
        }
    }
    return Status;
}

/* ScGetWorkingDirectory
 * Queries the current working directory path for the current module (See _MAXPATH) */
OsStatus_t
ScGetWorkingDirectory(
    _In_ char*  PathBuffer,
    _In_ size_t MaxLength)
{
    SystemModule_t* Module = GetCurrentModule();
    size_t          BytesToCopy = MaxLength;
    
    if (Module == NULL || PathBuffer == NULL || MaxLength == 0) {
        return OsError;
    }
    BytesToCopy = MIN(strlen(MStringRaw(Module->WorkingDirectory)) + 1, MaxLength);
    memcpy(PathBuffer, MStringRaw(Module->WorkingDirectory), BytesToCopy);
    return OsSuccess;
}

/* ScSetWorkingDirectory
 * Performs changes to the current working directory by canonicalizing the given 
 * path modifier or absolute path */
OsStatus_t
ScSetWorkingDirectory(
    _In_ const char* Path)
{
    SystemModule_t* Module = GetCurrentModule();
    MString_t*      Translated;

    if (Module == NULL || Path == NULL) {
        return OsError;
    }
    Translated = MStringCreate(Path, StrUTF8);
    MStringDestroy(Module->WorkingDirectory);
    Module->WorkingDirectory = Translated;
    return OsSuccess;
}

/* ScGetAssemblyDirectory
 * Queries the application path for the current process (See _MAXPATH) */
OsStatus_t
ScGetAssemblyDirectory(
    _In_ char*  PathBuffer,
    _In_ size_t MaxLength)
{
    SystemModule_t* Module = GetCurrentModule();
    size_t          BytesToCopy = MaxLength;

    if (Module == NULL || PathBuffer == NULL || MaxLength == 0) {
        return OsError;
    }
    if (strlen(MStringRaw(Module->BaseDirectory)) + 1 < MaxLength) {
        BytesToCopy = strlen(MStringRaw(Module->BaseDirectory)) + 1;
    }
    memcpy(PathBuffer, MStringRaw(Module->BaseDirectory), BytesToCopy);
    return OsSuccess;
}
