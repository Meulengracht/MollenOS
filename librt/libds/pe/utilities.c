/**
 * MollenOS
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

#include <ds/ds.h>
#include <ds/list.h>
#include <os/mollenos.h>
#include <ds/mstring.h>
#include <string.h>
#include <assert.h>
#include "pe.h"

#ifndef __TRACE
#undef dstrace
#define dstrace(...)
#endif

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
    foreach(i, ExportParent->Libraries) {
        PeExecutable_t *Library = i->value;
        if (MStringCompare(Library->Name, LibraryName, 1) == MSTRING_FULL_MATCH) {
            dstrace("Library %s was already resolved, increasing ref count", MStringRaw(Library->Name));
            Library->References++;
            Exports = Library;
            break;
        }
    }

    // Sanitize the exports, if its null we have to resolve the library
    if (Exports == NULL) {
        Status = PeLoadImage(ExportParent->Owner, ExportParent, LibraryName, &Exports);
        if (Status != OsSuccess) {
            dserror("Library %s could not be loaded %u", MStringRaw(LibraryName), Status);
        }
    }

    if (Exports == NULL) {
        dserror("Library %s was unable to be resolved", MStringRaw(LibraryName));
    }
    return Exports;
}

uintptr_t
PeResolveFunction(
    _In_ PeExecutable_t* Library, 
    _In_ const char*    Function)
{
    PeExportedFunction_t* Exports = Library->ExportedFunctions;
    if (Exports != NULL) {
        for (int i = 0; i < Library->NumberOfExportedFunctions; i++) {
            if (Exports[i].Name != NULL && !strcmp(Exports[i].Name, Function)) {
                return Exports[i].Address;
            }
        }
    }
    return 0;
}

OsStatus_t
PeGetModuleHandles(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount)
{
    int maxCount;
    int index;

    if (!executable || !moduleList || !moduleCount) {
        return OsInvalidParameters;
    }
    
    index    = 0;
    maxCount = *moduleCount;

    // Copy base over
    moduleList[index++] = (Handle_t)executable->VirtualAddress;
    if (executable->Libraries != NULL) {
        foreach(i, executable->Libraries) {
            PeExecutable_t *Library = i->value;
            moduleList[index++]     = (Handle_t)Library->VirtualAddress;
        
            if (index == maxCount) {
                break;
            }
        }
    }
    
    *moduleCount = index;
    return OsSuccess;
}

OsStatus_t
PeGetModuleEntryPoints(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount)
{
    int maxCount;
    int index;

    if (!executable || !moduleList || !moduleCount) {
        return OsInvalidParameters;
    }
    
    index    = 0;
    maxCount = *moduleCount;

    if (executable->Libraries != NULL) {
        foreach(i, executable->Libraries) {
            PeExecutable_t *Library = i->value;
            if (Library->EntryAddress != 0) {
                moduleList[index++] = (Handle_t)Library->EntryAddress;
            }
        
            if (index == maxCount) {
                break;
            }
        }
    }
    
    *moduleCount = index;
    return OsSuccess;
}
