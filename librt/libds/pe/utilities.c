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
 * PE/COFF Image Loader
 *    - Implements support for loading and processing pe/coff image formats
 *      and implemented as a part of libds to share between services and kernel
 */

#include <os/mollenos.h>
#include <ds/ds.h>
#include <assert.h>
#include "pe.h"

/* PeResolveLibrary
 * Resolves a dependancy or a given module path, a load address must be provided
 * together with a pe-file header to fill out and the parent that wants to resolve the library */
PeExecutable_t*
PeResolveLibrary(
    _In_    PeExecutable_t* Parent,
    _In_    PeExecutable_t* Image,
    _In_    MString_t*      LibraryName)
{
    PeExecutable_t* ExportParent = Parent;
    PeExecutable_t* Exports      = NULL;
    OsStatus_t Status;

    dstrace("PeResolveLibrary(Name %s)", MStringRaw(LibraryName));
    if (ExportParent == NULL) {
        ExportParent = Image;
    }

    // Before actually loading the file, we want to
    // try to locate the library in the parent first.
    foreach(Node, ExportParent->Libraries) {
        PeExecutable_t *Library = (PeExecutable_t*)Node->Data;
        if (MStringCompare(Library->Name, LibraryName, 1) == MSTRING_FULL_MATCH) {
            dstrace("Library %s was already resolved, increasing ref count", MStringRaw(Library->Name));
            Library->References++;
            Exports = Library;
            break;
        }
    }

    // Sanitize the exports, if its null we have to resolve the library
    if (Exports == NULL) {
        MString_t* FullPath;
        uint8_t*   Buffer;
        size_t     Size;

        Status = LoadFile(LibraryName, &FullPath, (void**)&Buffer, &Size);
        if (Status == OsSuccess) {
            Status = PeLoadImage(ExportParent, LibraryName, FullPath, Buffer, Size, &Exports);
        }
    }

    if (Exports == NULL) {
        ERROR("Library %s was unable to be resolved", MStringRaw(LibraryName));
    }
    return Exports;
}

/* PeResolveFunction
 * Resolves a function by name in the given pe image, the return
 * value is the address of the function. 0 If not found */
uintptr_t
PeResolveFunction(
    _In_ PeExecutable_t* Library, 
    _In_ const char*    Function)
{
    MCorePeExportFunction_t* Exports = Library->ExportedFunctions;
    if (Exports != NULL) {
        for (int i = 0; i < Library->NumberOfExportedFunctions; i++) {
            if (Exports[i].Name != NULL && !strcmp(Exports[i].Name, Function)) {
                return Exports[i].Address;
            }
        }
    }
    return 0;
}

/* PeGetModuleHandles
 * Retrieves a list of loaded module handles currently loaded for the process. */
OsStatus_t
PeGetModuleHandles(
    _In_  PeExecutable_t* Executable,
    _Out_ Handle_t        ModuleList[PROCESS_MAXMODULES])
{
    int Index = 0;

    if (Executable == NULL || ModuleList == NULL) {
        return OsError;
    }
    memset(&ModuleList[0], 0, sizeof(Handle_t) * PROCESS_MAXMODULES);

    // Copy base over
    ModuleList[Index++] = (Handle_t)Executable->VirtualAddress;
    if (Executable->Libraries != NULL) {
        foreach(Node, Executable->Libraries) {
            PeExecutable_t *Library = (PeExecutable_t*)Node->Data;
            ModuleList[Index++]     = (Handle_t)Library->VirtualAddress;
        }
    }
    return OsSuccess;
}

/* PeGetModuleEntryPoints
 * Retrieves a list of loaded module entry points currently loaded for the process. */
OsStatus_t
PeGetModuleEntryPoints(
    _In_  PeExecutable_t*   Executable,
    _Out_ Handle_t          ModuleList[PROCESS_MAXMODULES])
{
    int Index = 0;

    if (Executable == NULL || ModuleList == NULL) {
        return OsError;
    }
    memset(&ModuleList[0], 0, sizeof(Handle_t) * PROCESS_MAXMODULES);

    // Copy base over
    if (Executable->Libraries != NULL) {
        foreach(Node, Executable->Libraries) {
            PeExecutable_t *Library = (PeExecutable_t*)Node->Data;
            if (Library->EntryAddress != 0) {
                ModuleList[Index++] = (Handle_t)Library->EntryAddress;
            }
        }
    }
    return OsSuccess;
}
