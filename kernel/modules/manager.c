/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Kernel Module System
 *   - Implements loading and management of modules that exists on the initrd. 
 */
#define __MODULE "MODS"
//#define __TRACE

#include <system/interrupts.h>
#include <modules/modules.h>
#include <ds/collection.h>
#include <interrupts.h>
#include <debug.h>
#include <heap.h>

static Collection_t Modules  = COLLECTION_INIT(KeyInteger);

/* RegisterModule
 * Registers a new system module resource that is then available for the operating system
 * to use. The resource can be can be either an driver, service or a generic file. */
OsStatus_t
RegisterModule(
    _In_ const char*        Path,
    _In_ const void*        Data,
    _In_ SystemModuleType_t Type,
    _In_ DevInfo_t          VendorId,
    _In_ DevInfo_t          DeviceId,
    _In_ DevInfo_t          DeviceClass,
    _In_ DevInfo_t          DeviceSubclass)
{
    SystemModule_t* Module;
    DataKey_t       Key = { .Value.Integer = (int)Type };

    // Allocate a new module header and copy some values 
    Module       = (SystemModule_t*)kmalloc(sizeof(SystemModule_t));
    Module->Path = MStringCreate("rd:/", StrUTF8);
    MStringAppendString(Module->Path, Path);
    Module->Data = Data;

    Module->VendorId       = VendorId;
    Module->DeviceId       = DeviceId;
    Module->DeviceClass    = DeviceClass;
    Module->DeviceSubclass = DeviceSubclass;
    return CollectionAppend(&Modules, CollectionCreateNode(Key, Module));
}

/* SpawnServices
 * Loads all system services present in the initial ramdisk. */
void
SpawnServices(void)
{
    IntStatus_t IrqState;

    // Disable interrupts while doing this as
    // we are still the idle thread -> as soon as a new
    // work is spawned we hardly ever return to this
    IrqState = InterruptDisable();

    // Iterate module list and spawn all servers
    // then they will "run" the system for us
    foreach(Node, &Modules) {
        if (Node->Key.Value.Integer == (int)ServiceResource) {
            SystemModule_t* Module = (SystemModule_t*)Node->Data;
            CreateModule(Module->Path, 0, 0, 0, 0);
        }
    }
    InterruptRestoreState(IrqState);
}

/* GetModuleDataByPath
 * Retrieve a pointer to the file-buffer and its length based on 
 * the given <rd:/> path */
OsStatus_t
GetModuleDataByPath(
    _In_  MString_t* Path, 
    _Out_ void**     Buffer, 
    _Out_ size_t*    Length)
{
    MString_t* Token  = Path;
    OsStatus_t Result = OsError;
    int        Index;
    TRACE("GetModuleDataByPath(%s)", MStringRaw(Path));

    // Build the token we are looking for
    Index = MStringFindReverse(Path, '/', 0);
    if (Index != MSTRING_NOT_FOUND) {
        Token = MStringSubString(Path, Index + 1, -1);
    }
    TRACE("TokenToSearchFor(%s)", MStringRaw(Token));

    // Locate the module
    foreach(Node, &Modules) {
        SystemModule_t* Module = (SystemModule_t*)Node->Data;
        TRACE("Comparing(%s)To(%s)", MStringRaw(Token), MStringRaw(Module->Path));
        if (MStringCompare(Token, Module->Path, 1) != MSTRING_NO_MATCH) {
            *Buffer = (void*)Module->Data;
            *Length = Module->Header.LengthOfData;
            Result = OsSuccess;
            break;
        }
    }
    if (Index != MSTRING_NOT_FOUND) {
        MStringDestroy(Token);
    }
    return Result;
}

/* ModulesFindGeneric
 * Resolve a 'generic' driver by its device-type and/or
 * its device sub-type, this is generally used if there is no
 * vendor specific driver available for the device. Returns NULL
 * if none is available */
MCoreModule_t*
ModulesFindGeneric(
    _In_ DevInfo_t DeviceType, 
    _In_ DevInfo_t DeviceSubType)
{
    foreach(sNode, &Modules) {
        MCoreModule_t *Mod = (MCoreModule_t*)sNode->Data;
        if (Mod->Header.DeviceType == DeviceType
            && Mod->Header.DeviceSubType == DeviceSubType) {
            return Mod;
        }
    }
    return NULL;
}

/* ModulesFindSpecific
 * Resolve a specific driver by its vendorid and deviceid 
 * this is to ensure optimal module load. Returns NULL 
 * if none is available */
MCoreModule_t*
ModulesFindSpecific(
    _In_ DevInfo_t VendorId, 
    _In_ DevInfo_t DeviceId)
{
    // Sanitize the id's
    if (VendorId == 0) {
        return NULL;
    }
    foreach(sNode, &Modules) {
        MCoreModule_t *Mod = (MCoreModule_t*)sNode->Data;
        if (Mod->Header.VendorId == VendorId
            && Mod->Header.DeviceId == DeviceId) {
            return Mod;
        }
    }
    return NULL;
}

/* ModulesFindString
 * Resolve a module by its name. Returns NULL if none
 * is available */
MCoreModule_t*
ModulesFindString(
    _In_ MString_t *Module)
{
    foreach(sNode, &Modules) {
        MCoreModule_t *Mod = (MCoreModule_t*)sNode->Data;
        if (MStringCompare(Module, Mod->Name, 1) == MSTRING_FULL_MATCH) {
            return Mod;
        }
    }
    return NULL;
}
