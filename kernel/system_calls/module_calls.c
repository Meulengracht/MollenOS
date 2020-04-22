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

#include <assert.h>
#include <arch/utils.h>
#include <ds/mstring.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include "../../librt/libds/pe/pe.h"
#include <modules/manager.h>
#include <os/types/process.h>
#include <scheduler.h>
#include <threading.h>
#include <string.h>

OsStatus_t
ScModuleGetStartupInformation(
    _In_  ProcessStartupInformation_t* startupInformation, 
    _Out_ UUId_t*                      processIdOut,
    _In_  char*                        buffer,
    _In_  size_t                       bufferMaxLength)
{
    SystemModule_t* module          = GetCurrentModule();
    int             bufferIndex     = 0;
    size_t          bufferBytesLeft = bufferMaxLength;
    int             moduleCount     = 0;
    Handle_t        moduleList[16]; // no more is neccessary... but it is riscy
    OsStatus_t      status;

    if (!module) {
        return OsInvalidPermissions;
    }

    if (!startupInformation || !processIdOut ||
        !buffer || !bufferMaxLength) {
        return OsInvalidParameters;
    }

    *processIdOut = module->Handle;

    memset(startupInformation, 0, sizeof(ProcessStartupInformation_t));

    if (module->ArgumentBlock != NULL) {
        size_t bytesToCopy = MIN(bufferBytesLeft, module->ArgumentBlockLength);
        memcpy(&buffer[bufferIndex], module->ArgumentBlock, bytesToCopy);

        startupInformation->Arguments       = &buffer[bufferIndex];
        startupInformation->ArgumentsLength = bytesToCopy;

        bufferBytesLeft -= bytesToCopy;
        bufferIndex     += bytesToCopy;
    }

    if (module->InheritanceBlock != NULL) {
        size_t bytesToCopy = MIN(bufferBytesLeft, module->InheritanceBlockLength);
        memcpy(&buffer[bufferIndex], module->InheritanceBlock, bytesToCopy);

        startupInformation->Inheritation       = &buffer[bufferIndex];
        startupInformation->InheritationLength = bytesToCopy;

        bufferBytesLeft -= bytesToCopy;
        bufferIndex     += bytesToCopy;
    }
    
    status = PeGetModuleEntryPoints(module->Executable,
        moduleList, &moduleCount);
    assert(moduleCount < 16);
    if (status == OsSuccess) {
        size_t bytesToCopy = MIN(bufferBytesLeft, moduleCount * sizeof(Handle_t));
        memcpy(&buffer[bufferIndex], &moduleList[0], bytesToCopy);

        startupInformation->LibraryEntries       = &buffer[bufferIndex];
        startupInformation->LibraryEntriesLength = bytesToCopy;

        bufferBytesLeft -= bytesToCopy;
        bufferIndex     += bytesToCopy;
    }

    return OsSuccess;
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
    int             ModuleCount;
    if (Module == NULL) {
        return OsError;
    }
    return PeGetModuleHandles(Module->Executable, ModuleList, &ModuleCount);
}

OsStatus_t
ScModuleGetModuleEntryPoints(
    _In_ Handle_t ModuleList[PROCESS_MAXMODULES])
{
    SystemModule_t* Module = GetCurrentModule();
    int             ModuleCount;
    if (Module == NULL) {
        return OsError;
    }
    return PeGetModuleEntryPoints(Module->Executable, ModuleList, &ModuleCount);
}

OsStatus_t
ScModuleExit(
    _In_ int ExitCode)
{
    MCoreThread_t*  Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    SystemModule_t* Module = GetCurrentModule();
    OsStatus_t      Status = OsError;
    if (Module != NULL) {
        WARNING("Process %s terminated with code %i", MStringRaw(Module->Executable->Name), ExitCode);
        // Are we detached? Then call only thread cleanup
        if (Thread->ParentHandle == UUID_INVALID) {
            Status = TerminateThread(Thread->Handle, ExitCode, 1);
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
