/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 */
//#define __TRACE

#include <ddk/utils.h>
#include "include/vfs.h"
#include <os/sharedobject.h>
#include <stdlib.h>

/* Filesystem driver map
 * This for now needs to be static unfortunately
 * untill i figure out a smart protocol for it */
const char *_GlbFileSystemDrivers[] = {
	NULL,
	"$sys/drivers/filesystems/fat.dll",
	"$sys/drivers/filesystems/exfat.dll",
	"$sys/drivers/filesystems/ntfs.dll",
	"$sys/drivers/filesystems/hfs.dll",
	"$sys/drivers/filesystems/hpfs.dll",
	"mfs.dll",
	"$sys/drivers/filesystems/ext.dll"
};

/* VfsResolveFileSystem
 * Tries to resolve the given filesystem by locating
 * the appropriate driver library for the given type */
FileSystemModule_t*
VfsResolveFileSystem(
    _In_ FileSystem_t *FileSystem)
{
	FileSystemModule_t* Module = NULL;
	CollectionItem_t*   fNode = NULL;
	DataKey_t           Key = { 0 };

	// Trace
	TRACE("VfsResolveFileSystem(Type %u)", FileSystem->Type);

	// Iterate the module list and
	// try to locate an already loaded module
	_foreach(fNode, VfsGetModules()) {
		Module = (FileSystemModule_t*)fNode->Data;
		if (Module->Type == FileSystem->Type) {
			Module->References++;
			return Module;
		}
	}

	// Trace
	TRACE("New resolve - loading module");

	// Not found, allocate a new instance 
	Module = (FileSystemModule_t*)malloc(sizeof(FileSystemModule_t));
	if (!Module) {
		return NULL;
	}
	Module->Type = FileSystem->Type;
	Module->References = 1;
	Module->Handle = HANDLE_INVALID;

	// Resolve the library path and load it 
	if (FileSystem->Type != FSUnknown) {
        if (FileSystem->Type != FSMFS) {
            ERROR("Can't load modules other than onboard modules in ramdisk");
            free(Module);
            return NULL;
        }
		Module->Handle = SharedObjectLoad(_GlbFileSystemDrivers[(int)FileSystem->Type]);
	}

	// Were we able to resolve?
	if (Module->Handle == HANDLE_INVALID) {
		ERROR("Failed to load driver %s", _GlbFileSystemDrivers[(int)FileSystem->Type]);
		free(Module);
		return NULL;
	}

	// Trace
	TRACE("System was resolved, retrieving function table");

	// Resolve all functions, they MUST exist
	// - FsInitialize
	// - FsDestroy
	// - FsOpenFile
	// - FsCreateFile
	// - FsCloseFile
	// - FsDeletePath
	// - FsChangeFileSize
	// - FsOpenHandle
	// - FsCloseHandle
	// - FsReadFile
	// - FsWriteFile
	// - FsSeekFile
	Module->Initialize = (FsInitialize_t)
		SharedObjectGetFunction(Module->Handle, "FsInitialize");
	Module->Destroy = (FsDestroy_t)
		SharedObjectGetFunction(Module->Handle, "FsDestroy");
	Module->OpenEntry = (FsOpenEntry_t)
		SharedObjectGetFunction(Module->Handle, "FsOpenEntry");
	Module->CreatePath = (FsCreatePath_t)
		SharedObjectGetFunction(Module->Handle, "FsCreatePath");
	Module->CloseEntry = (FsCloseEntry_t)
		SharedObjectGetFunction(Module->Handle, "FsCloseEntry");
	Module->DeleteEntry = (FsDeleteEntry_t)
		SharedObjectGetFunction(Module->Handle, "FsDeleteEntry");
	Module->ChangeFileSize = (FsChangeFileSize_t)
		SharedObjectGetFunction(Module->Handle, "FsChangeFileSize");
	Module->OpenHandle = (FsOpenHandle_t)
		SharedObjectGetFunction(Module->Handle, "FsOpenHandle");
	Module->CloseHandle = (FsCloseHandle_t)
		SharedObjectGetFunction(Module->Handle, "FsCloseHandle");
	Module->ReadEntry = (FsReadEntry_t)
		SharedObjectGetFunction(Module->Handle, "FsReadEntry");
	Module->WriteEntry = (FsWriteEntry_t)
		SharedObjectGetFunction(Module->Handle, "FsWriteEntry");
	Module->SeekInEntry = (FsSeekInEntry_t)
		SharedObjectGetFunction(Module->Handle, "FsSeekInEntry");

	// Sanitize functions
	if (Module->Initialize == NULL || Module->Destroy == NULL ||
        Module->OpenEntry == NULL || Module->CreatePath == NULL ||
        Module->CloseEntry == NULL || Module->DeleteEntry == NULL ||
        Module->ChangeFileSize == NULL || Module->OpenHandle == NULL ||
        Module->CloseHandle == NULL || Module->ReadEntry == NULL ||
        Module->WriteEntry == NULL || Module->SeekInEntry == NULL)
    {
	    ERROR("Missing functions in module table");
		SharedObjectUnload(Module->Handle);
		free(Module);
		return NULL;
	}

	// Trace
	TRACE("Function table present, loading was successful");
	CollectionAppend(VfsGetModules(), CollectionCreateNode(Key, Module));
	return Module;
}
