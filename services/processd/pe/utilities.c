/**
 * Copyright 2022, Philip Meulengracht
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

//#define __TRACE

#include <ds/ds.h>
#include <ds/list.h>
#include <os/mollenos.h>
#include <ds/mstring.h>
#include <string.h>
#include <assert.h>
#include "pe.h"

PeExecutable_t*
PeResolveLibrary(
    _In_    PeExecutable_t* Parent,
    _In_    PeExecutable_t* Image,
    _In_    mstring_t*      LibraryName)
{
    PeExecutable_t* ExportParent = Parent;
    PeExecutable_t* Exports      = NULL;
    oserr_t Status;

    //dstrace("PeResolveLibrary(Name %ms)", LibraryName);
    if (ExportParent == NULL) {
        ExportParent = Image;
    }

    // Before actually loading the file, we want to
    // try to locate the library in the parent first.
    foreach(i, ExportParent->Libraries) {
        PeExecutable_t *Library = i->value;
        if (!mstr_cmp(Library->Name, LibraryName)) {
            dstrace("Library %ms was already resolved, increasing ref count", Library->Name);
            Library->References++;
            Exports = Library;
            break;
        }
    }

    // Sanitize the exports, if its null we have to resolve the library
    if (Exports == NULL) {
        Status = PeLoadImage(ExportParent->Owner, ExportParent, LibraryName, &Exports);
        if (Status != OS_EOK) {
            dserror("Library %ms could not be loaded %u", LibraryName, Status);
        }
    }

    if (Exports == NULL) {
        dserror("Library %ms was unable to be resolved", LibraryName);
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

oserr_t
PeGetModuleHandles(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount)
{
    int maxCount;
    int index;

    if (!executable || !moduleList || !moduleCount) {
        return OS_EINVALPARAMS;
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
    return OS_EOK;
}

oserr_t
PeGetModuleEntryPoints(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount)
{
    int maxCount;
    int index;

    if (!executable || !moduleList || !moduleCount) {
        return OS_EINVALPARAMS;
    }
    
    index    = 0;
    maxCount = *moduleCount;

    if (executable->Libraries != NULL) {
        foreach(i, executable->Libraries) {
            PeExecutable_t *Library = i->value;
            
            dstrace("[pe] [get_modules] %i: library %ms => 0x%" PRIxIN,
                index, Library->Name, Library->EntryAddress);
            if (Library->EntryAddress != 0) {
                moduleList[index++] = (Handle_t)Library->EntryAddress;
            }
        
            if (index == maxCount) {
                break;
            }
        }
    }
    
    *moduleCount = index;
    return OS_EOK;
}
