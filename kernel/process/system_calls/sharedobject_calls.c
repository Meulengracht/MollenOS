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

#include <process/process.h>
#include <process/pe.h>
#include <ds/mstring.h>

/* ScSharedObjectLoad
 * Load a shared object given a path 
 * path must exists otherwise NULL is returned */
Handle_t
ScSharedObjectLoad(
    _In_  const char*   SharedObject)
{
    SystemProcess_t*    Process     = GetCurrentProcess();
    Handle_t            Handle      = HANDLE_INVALID;
    MString_t*          Path;
    uintptr_t           BaseAddress;
    if (Process == NULL) {
        return HANDLE_INVALID;
    }

    // Sanitize the given shared-object path
    // If null, get handle to current assembly
    if (SharedObject == NULL) {
        return HANDLE_GLOBAL;
    }
    Path = MStringCreate((void*)SharedObject, StrUTF8);

    // Try to resolve the library
    BaseAddress = Process->NextLoadingAddress;
    Handle      = (Handle_t)PeResolveLibrary(Process->Executable, NULL, 
        Path, &BaseAddress);
    Process->NextLoadingAddress = BaseAddress;
    MStringDestroy(Path);
    return Handle;
}

/* ScSharedObjectGetFunction
 * Load a function-address given an shared object
 * handle and a function name, function must exist
 * otherwise null is returned */
uintptr_t
ScSharedObjectGetFunction(
    _In_ Handle_t       Handle, 
    _In_ const char*    Function)
{
    if (Handle == HANDLE_INVALID || Function == NULL) {
        return 0;
    }

    // If the handle is the handle_global, search all loaded
    // libraries for the symbol
    if (Handle == HANDLE_GLOBAL) {
        SystemProcess_t*    Process = GetCurrentProcess();
        uintptr_t           Address = 0;
        if (Process != NULL) {
            Address = PeResolveFunction(Process->Executable, Function);
            if (!Address) {
                foreach(Node, Process->Executable->LoadedLibraries) {
                    Address = PeResolveFunction((MCorePeFile_t*)Node->Data, Function);
                    if (Address != 0) {
                        break;
                    }
                }
            }
        }
        return Address;
    }
    else {
        return PeResolveFunction((MCorePeFile_t*)Handle, Function);
    }
}

/* Unloads a valid shared object handle
 * returns 0 on success */
OsStatus_t
ScSharedObjectUnload(
    _In_  Handle_t  Handle)
{
    SystemProcess_t* Process = GetCurrentProcess();
    if (Process == NULL || Handle == HANDLE_INVALID) {
        return OsError;
    }
    if (Handle == HANDLE_GLOBAL) { // Never close running handle
        return OsSuccess;
    }
    return PeUnloadLibrary(Process->Executable, (MCorePeFile_t*)Handle);
}
