/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * File Manager Service
 * - Handles all file related services and disk services
 */

//#define __TRACE

#include <ddk/utils.h>
#include <vfs/filesystem_module.h>
#include <os/sharedobject.h>
#include <stdlib.h>

static list_t      g_modulesLoaded = LIST_INIT;
static const char* g_fsModules[] = {
	NULL,
	"$sys/drivers/filesystems/fat.dll",
	"$sys/drivers/filesystems/exfat.dll",
	"$sys/drivers/filesystems/ntfs.dll",
	"$sys/drivers/filesystems/hfs.dll",
	"$sys/drivers/filesystems/hpfs.dll",
	"mfs.dll",
	"$sys/drivers/filesystems/ext.dll"
};

static inline FileSystemModule_t* __GetLoadedModule(enum FileSystemType type)
{
    element_t* header = list_find(&g_modulesLoaded, (void*)(uintptr_t)type);
    return header ? header->value : NULL;
}

FileSystemModule_t*
VfsLoadModule(
        _In_ enum FileSystemType type)
{
	FileSystemModule_t* module;
	Handle_t            handle;

	if (type == FileSystemType_UNKNOWN) {
	    return NULL;
	}

    TRACE("[vfs] [load_module] %u", fileSystem->Type);
    module = __GetLoadedModule(type);
    if (module) {
        return module;
    }

    handle = SharedObjectLoad(g_fsModules[(int)type]);
    if (handle == HANDLE_INVALID) {
        ERROR("[vfs] [load_module] failed to load %s", g_fsModules[(int)type]);
        return NULL;
    }

    // Not found, allocate a new instance
	module = (FileSystemModule_t*)malloc(sizeof(FileSystemModule_t));
	if (!module) {
		return NULL;
	}

	ELEMENT_INIT(&module->header, (uintptr_t)type, module);
    module->type       = type;
    module->references = 1;
    module->handle     = handle;

	TRACE("[vfs] [load_module] retrieving function table");

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
	module->Initialize     = (FsInitialize_t)SharedObjectGetFunction(handle, "FsInitialize");
    module->Destroy        = (FsDestroy_t)SharedObjectGetFunction(handle, "FsDestroy");
    module->OpenEntry      = (FsOpenEntry_t)SharedObjectGetFunction(handle, "FsOpenEntry");
    module->CreatePath     = (FsCreatePath_t)SharedObjectGetFunction(handle, "FsCreatePath");
    module->CloseEntry     = (FsCloseEntry_t)SharedObjectGetFunction(handle, "FsCloseEntry");
    module->DeleteEntry    = (FsDeleteEntry_t)SharedObjectGetFunction(handle, "FsDeleteEntry");
    module->ChangeFileSize = (FsChangeFileSize_t)SharedObjectGetFunction(handle, "FsChangeFileSize");
    module->OpenHandle     = (FsOpenHandle_t)SharedObjectGetFunction(handle, "FsOpenHandle");
    module->CloseHandle    = (FsCloseHandle_t)SharedObjectGetFunction(handle, "FsCloseHandle");
    module->ReadEntry      = (FsReadEntry_t)SharedObjectGetFunction(handle, "FsReadEntry");
    module->WriteEntry     = (FsWriteEntry_t)SharedObjectGetFunction(handle, "FsWriteEntry");
    module->SeekInEntry    = (FsSeekInEntry_t)SharedObjectGetFunction(handle, "FsSeekInEntry");

	// Sanitize functions
	if (module->Initialize == NULL || module->Destroy == NULL ||
        module->OpenEntry == NULL || module->CreatePath == NULL ||
        module->CloseEntry == NULL || module->DeleteEntry == NULL ||
        module->ChangeFileSize == NULL || module->OpenHandle == NULL ||
        module->CloseHandle == NULL || module->ReadEntry == NULL ||
        module->WriteEntry == NULL || module->SeekInEntry == NULL)
    {
	    ERROR("[vfs] [load_module] missing functions in module table");
		SharedObjectUnload(handle);
		free(module);
		return NULL;
	}

	list_append(&g_modulesLoaded, &module->header);
	return module;
}

void
VfsUnloadModule(
        _In_ FileSystemModule_t* module)
{
    if (!module) {
        return;
    }

    module->references--;
    if (module->references <= 0) {
        // Unload? Or keep loaded?
    }
}
