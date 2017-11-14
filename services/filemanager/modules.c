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

/* Includes
 * - System */
#include "include/vfs.h"
#include <os/utils.h>

/* Includes
 * - Library */
#include <stdlib.h>

/* Filesystem driver map
 * This for now needs to be static unfortunately
 * untill i figure out a smart protocol for it */
const char *_GlbFileSystemDrivers[] = {
	NULL,
	"%sys%/drivers/filesystems/fat.dll",
	"%sys%/drivers/filesystems/exfat.dll",
	"%sys%/drivers/filesystems/ntfs.dll",
	"%sys%/drivers/filesystems/hfs.dll",
	"%sys%/drivers/filesystems/hpfs.dll",
	"rd:/mfs.dll",
	"%sys%/drivers/filesystems/ext.dll"
};

/* VfsResolveFileSystem
 * Tries to resolve the given filesystem by locating
 * the appropriate driver library for the given type */
FileSystemModule_t *VfsResolveFileSystem(FileSystem_t *FileSystem)
{
	// Variables
	FileSystemModule_t *Module = NULL;
	CollectionItem_t *fNode = NULL;
	DataKey_t Key;

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
	Module->Type = FileSystem->Type;
	Module->References = 1;
	Module->Handle = HANDLE_INVALID;

	// Resolve the library path and load it 
	if (FileSystem->Type != FSUnknown) {
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
	// - FsOpenHandle
	// - FsCloseHandle
	// - FsReadFile
	// - FsWriteFile
	// - FsSeekFile
	// - FsChangeFileSize
	// - FsDeleteFile
	// - FsQueryFile 
	Module->Initialize = (FsInitialize_t)
		SharedObjectGetFunction(Module->Handle, "FsInitialize");
	Module->Destroy = (FsDestroy_t)
		SharedObjectGetFunction(Module->Handle, "FsDestroy");
	Module->OpenFile = (FsOpenFile_t)
		SharedObjectGetFunction(Module->Handle, "FsOpenFile");
	Module->CreateFile = (FsCreateFile_t)
		SharedObjectGetFunction(Module->Handle, "FsCreateFile");
	Module->CloseFile = (FsCloseFile_t)
		SharedObjectGetFunction(Module->Handle, "FsCloseFile");
	Module->OpenHandle = (FsOpenHandle_t)
		SharedObjectGetFunction(Module->Handle, "FsOpenHandle");
	Module->CloseHandle = (FsCloseHandle_t)
		SharedObjectGetFunction(Module->Handle, "FsCloseHandle");
	Module->ReadFile = (FsReadFile_t)
		SharedObjectGetFunction(Module->Handle, "FsReadFile");
	Module->WriteFile = (FsWriteFile_t)
		SharedObjectGetFunction(Module->Handle, "FsWriteFile");
	Module->SeekFile = (FsSeekFile_t)
		SharedObjectGetFunction(Module->Handle, "FsSeekFile");
	Module->ChangeFileSize = (FsChangeFileSize_t)
		SharedObjectGetFunction(Module->Handle, "FsChangeFileSize");
	Module->DeleteFile = (FsDeleteFile_t)
		SharedObjectGetFunction(Module->Handle, "FsDeleteFile");
	Module->QueryFile = (FsQueryFile_t)
		SharedObjectGetFunction(Module->Handle, "FsQueryFile");

	// Sanitize functions
	if (Module->Initialize == NULL
		|| Module->Destroy == NULL
		|| Module->OpenFile == NULL
		|| Module->CreateFile == NULL
		|| Module->CloseFile == NULL
		|| Module->OpenHandle == NULL
		|| Module->CloseHandle == NULL
		|| Module->ReadFile == NULL
		|| Module->WriteFile == NULL
		|| Module->SeekFile == NULL
		|| Module->DeleteFile == NULL
		|| Module->QueryFile == NULL) {
		SharedObjectUnload(Module->Handle);
		free(Module);
		return NULL;
	}

	// Trace
	TRACE("Function table present, loading was successful");

	// Last thing is to add it to the list
	Key.Value = 0;
	CollectionAppend(VfsGetModules(), CollectionCreateNode(Key, Module));

	// Return the newly created module
	return Module;
}
