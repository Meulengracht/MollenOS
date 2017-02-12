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

/* Includes
 * - System */
#include "../include/vfs.h"
#include "../include/gpt.h"
#include "../include/mbr.h"

/* VfsInstallFileSystem 
 * - Register the given fs with our system and run init
 *   if neccessary */
OsStatus_t VfsInstallFileSystem(FileSystemDisk_t *Disk)
{
	/* Ready the buffer */
	DataKey_t Key;
	char IdentBuffer[8];
	memset(IdentBuffer, 0, 8);

	/* Copy the storage ident over 
	 * We use St for hard media, and Rm for removables */
	strcpy(IdentBuffer, "St");
	itoa(GlbFileSystemId, (IdentBuffer + 2), 10);

	/* Construct the identifier */
	Fs->Identifier = MStringCreate(&IdentBuffer, StrASCII);

	/* Add to list */
	Key.Value = (int)Fs->DiskId;
	ListAppend(GlbFileSystems, ListCreateNode(Key, Key, Fs));

	/* Increament */
	GlbFileSystemId++;

	/* Start init? */
	if ((Fs->Flags & VFS_MAIN_DRIVE)
		&& !GlbVfsInitHasRun)
	{
		/* Process Request */
		MCorePhoenixRequest_t *ProcRequest
			= (MCorePhoenixRequest_t*)kmalloc(sizeof(MCorePhoenixRequest_t));

		/* Reset request */
		memset(ProcRequest, 0, sizeof(MCorePhoenixRequest_t));

		/* Print */
		LogInformation("VFSM", "Boot Drive Detected, Running Init");

		/* Append init path */
		MString_t *Path = MStringCreate((void*)MStringRaw(Fs->Identifier), StrUTF8);
		MStringAppendCharacters(Path, FILESYSTEM_INIT, StrUTF8);

		/* Create Request */
		ProcRequest->Base.Type = AshSpawnProcess;
		ProcRequest->Path = Path;
		ProcRequest->Arguments.String = NULL;
		ProcRequest->Base.Cleanup = 1;

		/* Send */
		PhoenixCreateRequest(ProcRequest);

		/* Set */
		GlbVfsInitHasRun = 1;
	}
}

/* DiskDetectFileSystem
 * Detectes the kind of filesystem at the given absolute sector 
 * with the given sector count. It then loads the correct driver
 * and installs it */
OsStatus_t DiskDetectFileSystem(FileSystemDisk_t *Disk,
	uint64_t StartSector, uint64_t SectorCount)
{

}

/* DiskDetectLayout
 * Detects the kind of layout on the disk, be it
 * MBR or GPT layout, if there is no layout it returns
 * OsError to indicate the entire disk is a FS */
OsStatus_t DiskDetectLayout(FileSystemDisk_t *Disk)
{
	/* Variables */
	BufferObject_t *Buffer = NULL;
	GptHeader_t *Gpt = NULL;
	OsStatus_t Result;

	/* Allocate a generic transfer buffer for disk operations
	 * on the given disk, we need it to parse the disk */
	Buffer = CreateBuffer(Disk->Descriptor.SectorSize);

	/* In order to detect the schema that is used
	 * for the disk - we can easily just read sector LBA 1
	 * and look for the GPT signature */
	if (DiskRead(Disk->Driver, Disk->Device, 1, Buffer->Physical, 1) != OsNoError) {
		DestroyBuffer(Buffer);
		return OsError;
	}

	/* Initiate the gpt pointer directly from the buffer-object
	 * to avoid doing double allocates when its not needed */
	Gpt = (GptHeader_t*)Buffer->Virtual;

	/* Check the GPT signature if it matches 
	 * - If it doesn't match, it can only be a MBR disk */
	if (!strncmp((const char*)&Gpt->Signature[0], GPT_SIGNATURE, 8)) {
		Result = GptEnumerate(Disk, Buffer);
	}
	else {
		Result = MbrEnumerate(Disk, Buffer);
	}

	/* Cleanup buffer */
	DestroyBuffer(Buffer);

	/* Return the previous result */
	return Result;
}
