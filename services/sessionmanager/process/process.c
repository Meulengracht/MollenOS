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
 * MollenOS Service - Session Manager
 * - Contains the implementation of the session-manager which keeps track
 *   of all users and their running applications.
 */

#include "process.h"
#include <os/buffer.h>
#include <os/utils.h>
#include <os/file.h>
#include <stdlib.h>

typedef struct _SessionProcessPackage {
    SessionProcess_t* Process;
    uint8_t*          FileBuffer;
    size_t            FileBufferLength;
} SessionProcessPackage_t;

/* LoadFile
 * Utility function to load a file into memory. The file is cached if it fits into memory and
 * we can find a space for it to improve respawnability of processes and libraries. */
OsStatus_t
LoadFile(
    _In_  const char*   Path,
    _Out_ char**        FullPath,
    _Out_ void**        Data,
    _Out_ size_t*       Length)
{
    FileSystemCode_t    FsCode;
    UUId_t              fHandle;
    DmaBuffer_t*        TransferBuffer;
    LargeInteger_t      QueriedSize     = { { 0 } };
    void*               fBuffer         = NULL;
    size_t fRead = 0, fIndex = 0;
    size_t fSize = 0;

    // Open the file as read-only
    FsCode = OpenFile(Path, 0, __FILE_READ_ACCESS, &fHandle);
    if (FsCode != FsOk) {
        ERROR("Invalid path given: %s", Path);
        return OsError;
    }

    if (GetFileSize(fHandle, &QueriedSize.u.LowPart, NULL) != OsSuccess) {
        ERROR("Failed to retrieve the file size");
        CloseFile(fHandle);
        return OsError;
    }

    if (FullPath != NULL) {
        *FullPath = (char*)malloc(_MAXPATH);
        memset((void*)*FullPath, 0, _MAXPATH);
        if (GetFilePath(fHandle, *FullPath, _MAXPATH) != OsSuccess) {
            ERROR("Failed to query file handle for full path");
            kfree((void*)*FullPath);
            CloseFile(fHandle);
            return OsError;
        }
    }

    fSize = (size_t)QueriedSize.QuadPart;
    if (fSize != 0) {
        TransferBuffer = CreateBuffer(UUID_INVALID, fSize);
        if (TransferBuffer == NULL) {
            ERROR("Failed to create a memory buffer");
            CloseFile(fHandle);
            return OsError;
        }
        
        fBuffer = malloc(fSize);
        if (fBuffer == NULL) {
            ERROR("Failed to allocate resources for file-loading");
            CloseFile(fHandle);
            return OsError;
        }

        FsCode = ReadFile(fHandle, TransferBuffer->Handle, fSize, &fIndex, &fRead);
        if (FsCode != FsOk) {
            ERROR("Failed to read file, code %i", FsCode);
            kfree(fBuffer);
            CloseFile(fHandle);
            return OsError;
        }
        memcpy(fBuffer, (const void*)TransferBuffer->Address, fRead);
        DestroyBuffer(TransferBuffer);
    }
    CloseFile(fHandle);
    *Data   = fBuffer;
    *Length = fSize;
    return OsSuccess;
}

/* HandleProcessStartupInformation
 * Creates neccessary kernel copies of the process startup information, as well as
 * validating the structure. */
void
HandleProcessStartupInformation(
    _In_ SessionProcess_t*            Process,
    _In_ ProcessStartupInformation_t* StartupInformation)
{
    char* ArgumentBuffer;

    // Handle startup information
    if (StartupInformation->ArgumentPointer != NULL && StartupInformation->ArgumentLength != 0) {
        ArgumentBuffer = (char*)kmalloc(MStringSize(Process->Path) + 1 + StartupInformation->ArgumentLength + 1);

        memcpy(ArgumentBuffer, MStringRaw(Process->Path), MStringSize(Process->Path));
        ArgumentBuffer[MStringSize(Process->Path)] = ' ';
        
        memcpy(ArgumentBuffer + MStringSize(Process->Path) + 1,
            StartupInformation->ArgumentPointer, StartupInformation->ArgumentLength);
        ArgumentBuffer[MStringSize(Process->Path) + 1 + StartupInformation->ArgumentLength] = '\0';
        
        Process->StartupInformation.ArgumentPointer = ArgumentBuffer;
        Process->StartupInformation.ArgumentLength  = MStringSize(Process->Path) + 1 + StartupInformation->ArgumentLength + 1;
    }
    else {
        ArgumentBuffer = (char*)kmalloc(MStringSize(Process->Path) + 1);
        memcpy(ArgumentBuffer, MStringRaw(Process->Path), MStringSize(Process->Path) + 1);

        Process->StartupInformation.ArgumentPointer = ArgumentBuffer;
        Process->StartupInformation.ArgumentLength  = MStringSize(Process->Path) + 1;
    }

    // Handle the inheritance block
    if (StartupInformation->InheritanceBlockPointer != NULL && StartupInformation->InheritanceBlockLength != 0) {
        void* InheritanceBlock = kmalloc(StartupInformation->InheritanceBlockLength);
        memcpy(InheritanceBlock, StartupInformation->InheritanceBlockPointer, StartupInformation->InheritanceBlockLength);
        Process->StartupInformation.InheritanceBlockPointer = InheritanceBlock;
        Process->StartupInformation.InheritanceBlockLength  = StartupInformation->InheritanceBlockLength;
    }
}

/* CreateProcess
 * Spawns a new process, which can be configured through the parameters. */
OsStatus_t
CreateProcess(
    _In_  const char*                  Path,
    _In_  ProcessStartupInformation_t* Parameters,
    _Out_ UUId_t*                      Handle)
{
    SessionProcessPackage_t* Package;
    SessionProcess_t*        Process;
    int                      Index;
    UUId_t                   ThreadId;

    assert(Path != NULL);
    assert(Handle != NULL);

    Process = (SessionProcess_t*)malloc(sizeof(SessionProcess_t));
    Package = (SessionProcessPackage_t*)malloc(sizeof(SessionProcessPackage_t));
    assert(Process != NULL);
    assert(Package != NULL);
    memset(Process, 0, sizeof(SessionProcess_t));
    
    // Start out by trying to resolve the process path, otherwise just abort
    if (LoadFile(Process, Package, Path) != OsSuccess) {
        ERROR(" > failed to resolve process path");
        kfree(Process);
        return OsDoesNotExist;
    }

    // Split path, even if a / is not found
    // it won't fail, since -1 + 1 = 0, so we just copy the entire string
    Index                       = MStringFindReverse(Process->Path, '/', 0);
    Process->WorkingDirectory   = MStringSubString(Process->Path, 0, Index);
    Process->BaseDirectory      = MStringSubString(Process->Path, 0, Index);
    Process->Name               = MStringSubString(Process->Path, Index + 1, -1);
    Package->ProcessHandle      = CreateHandle(HandleTypeProcess, HandleSynchronize, Process);
    HandleProcessStartupInformation(Process, StartupInformation);

    *Handle  = Package->ProcessHandle;
    ThreadId = ThreadingCreateThread(MStringRaw(Process->Name), ProcessThreadEntry, Package, THREADING_USERMODE);
    ThreadingDetachThread(ThreadId);
    return OsSuccess;
}
