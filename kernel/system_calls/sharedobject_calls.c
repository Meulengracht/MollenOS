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
#include <ds/mstring.h>
#include <debug.h>

OsStatus_t
ScSharedObjectLoad(
    _In_  const char* SoName,
    _Out_ Handle_t*   HandleOut)
{
    SystemModule_t* Module = GetCurrentModule();
    MString_t*      Path;
    OsStatus_t      Status;
    
    if (Module == NULL) {
        return OsInvalidPermissions;
    }

    // Sanitize the given shared-object path
    // If null, get handle to current assembly
    if (SoName == NULL) {
        *HandleOut = HANDLE_GLOBAL;
        return OsSuccess;
    }

    Path   = MStringCreate(SoName, StrUTF8);
    Status = PeLoadImage(UUID_INVALID, Module->Executable, Path, (PeExecutable_t**)HandleOut);
    MStringDestroy(Path);
    return Status;
}

uintptr_t
ScSharedObjectGetFunction(
    _In_ Handle_t    Handle, 
    _In_ const char* Function)
{
    if (Handle == HANDLE_INVALID || Function == NULL) {
        return 0;
    }

    // If the handle is the handle_global, search all loaded
    // libraries for the symbol
    if (Handle == HANDLE_GLOBAL) {
        SystemModule_t* Module  = GetCurrentModule();
        uintptr_t       Address = 0;
        if (Module != NULL) {
            Address = PeResolveFunction(Module->Executable, Function);
            if (!Address) {
                foreach(Node, Module->Executable->Libraries) {
                    Address = PeResolveFunction((PeExecutable_t*)Node->Data, Function);
                    if (Address != 0) {
                        break;
                    }
                }
            }
        }
        return Address;
    }
    else {
        return PeResolveFunction((PeExecutable_t*)Handle, Function);
    }
}

OsStatus_t
ScSharedObjectUnload(
    _In_ Handle_t Handle)
{
    SystemModule_t* Module = GetCurrentModule();
    if (Module == NULL || Handle == HANDLE_INVALID) {
        return OsError;
    }
    if (Handle == HANDLE_GLOBAL) { // Never close running handle
        return OsSuccess;
    }
    return PeUnloadLibrary(Module->Executable, (PeExecutable_t*)Handle);
}
