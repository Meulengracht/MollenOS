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
#include <os/driver/contracts/disk.h>
#include <os/driver/file/vfs.h>
#include <ds/list.h>

/* Includes
 * - C-Library */
#include <string.h>
#include <ctype.h>

/* Externs 
 * We import from VFS */
extern List_t *GlbFileSystems;
extern int GlbVfsInitHasRun;
extern int GlbFileSystemId;

/* VfsInstallFileSystem 
 * - Register the given fs with our system and run init
 *   if neccessary */
void VfsInstallFileSystem(MCoreFileSystem_t *Fs)
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

/* VfsParsePartitionTable 
 * - Partition table parser function for disks 
 *   and parses only MBR, not GPT */
int VfsParsePartitionTable(UUId_t DiskId, uint64_t SectorBase,
	uint64_t SectorCount, size_t SectorSize)
{
	/* Allocate structures */
	void *TmpBuffer = (void*)kmalloc(SectorSize);
//	MCoreModule_t *Module = NULL;
	MCoreMasterBootRecord_t *Mbr = NULL;
	MCoreDeviceRequest_t Request;
	int PartitionCount = 0;
	int i;

	/* Null out request */
	memset(&Request, 0, sizeof(MCoreDeviceRequest_t));

	/* Read sector */
	Request.Base.Type = RequestRead;
	Request.DeviceId = DiskId;
	Request.SectorLBA = SectorBase;
	Request.Buffer = (uint8_t*)TmpBuffer;
	Request.Length = SectorSize;

	/* Create & Wait */
	//DmCreateRequest(&Request);
	//DmWaitRequest(&Request, 0);

	/* Sanity */
	if (Request.Base.State != EventOk)
	{
		/* Error */
		LogFatal("VFSM", "REGISTERDISK: Error reading from disk - 0x%x\n", Request.ErrType);
		kfree(TmpBuffer);
		return 0;
	}

	/* We don't need sector count here */
	_CRT_UNUSED(SectorCount);

	/* Cast */
	Mbr = (MCoreMasterBootRecord_t*)TmpBuffer;

	/* Valid partition table? */
	for (i = 0; i < 4; i++)
	{
		/* Is it an active partition? */
		if (Mbr->Partitions[i].Status == PARTITION_ACTIVE)
		{
			/* Inc */
			PartitionCount++;

			/* Allocate a filesystem structure */
			MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)kmalloc(sizeof(MCoreFileSystem_t));
			Fs->State = VfsStateInit;
			Fs->DiskId = DiskId;
			Fs->ExtendedData = NULL;
			Fs->SectorStart = SectorBase + Mbr->Partitions[i].LbaSector;
			Fs->SectorCount = Mbr->Partitions[i].LbaSize;
			Fs->Id = GlbFileSystemId;
			Fs->SectorSize = SectorSize;

			/* Check extended partitions first */
			if (Mbr->Partitions[i].Type == 0x05)
			{
				/* Extended - CHS */
			}
			else if (Mbr->Partitions[i].Type == 0x0F
				|| Mbr->Partitions[i].Type == 0xCF)
			{
				/* Extended - LBA */
				PartitionCount += VfsParsePartitionTable(DiskId,
					SectorBase + Mbr->Partitions[i].LbaSector, Mbr->Partitions[i].LbaSize, SectorSize);
			}
			else if (Mbr->Partitions[i].Type == 0xEE)
			{
				/* GPT Formatted */
			}

			/* Check MFS */
			else if (Mbr->Partitions[i].Type == 0x61)
			{
				/* MFS 1 */
				//TODO
				//Module = ModuleFindGeneric(MODULE_FILESYSTEM, FILESYSTEM_MFS);

				/* Load */
//				if (Module != NULL)
//					ModuleLoad(Module, Fs);
			}

			/* Check FAT */
			else if (Mbr->Partitions[i].Type == 0x1
				|| Mbr->Partitions[i].Type == 0x6
				|| Mbr->Partitions[i].Type == 0x8 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x11 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x14 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x24 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x56 /* Might be FAT16 */
				|| Mbr->Partitions[i].Type == 0x8D
				|| Mbr->Partitions[i].Type == 0xAA
				|| Mbr->Partitions[i].Type == 0xC1
				|| Mbr->Partitions[i].Type == 0xD1
				|| Mbr->Partitions[i].Type == 0xE1
				|| Mbr->Partitions[i].Type == 0xE5
				|| Mbr->Partitions[i].Type == 0xEF
				|| Mbr->Partitions[i].Type == 0xF2)
			{
				/* Fat-12 */
			}
			else if (Mbr->Partitions[i].Type == 0x4
				|| Mbr->Partitions[i].Type == 0x6
				|| Mbr->Partitions[i].Type == 0xE
				|| Mbr->Partitions[i].Type == 0x16
				|| Mbr->Partitions[i].Type == 0x1E
				|| Mbr->Partitions[i].Type == 0x90
				|| Mbr->Partitions[i].Type == 0x92
				|| Mbr->Partitions[i].Type == 0x9A
				|| Mbr->Partitions[i].Type == 0xC4
				|| Mbr->Partitions[i].Type == 0xC6
				|| Mbr->Partitions[i].Type == 0xCE
				|| Mbr->Partitions[i].Type == 0xD4
				|| Mbr->Partitions[i].Type == 0xD6)
			{
				/* Fat16 */
			}
			else if (Mbr->Partitions[i].Type == 0x0B /* CHS */
				|| Mbr->Partitions[i].Type == 0x0C /* LBA */
				|| Mbr->Partitions[i].Type == 0x27
				|| Mbr->Partitions[i].Type == 0xCB
				|| Mbr->Partitions[i].Type == 0xCC)
			{
				/* Fat32 */
			}

			/* Lastly */
			if (Fs->State == VfsStateActive)
				VfsInstallFileSystem(Fs);
			else
				kfree(Fs);
		}
	}

	/* Done */
	kfree(TmpBuffer);
	return PartitionCount;
}

/* RegisterDisk
 * Registers a disk with the file-manager and it will
 * automatically be parsed (MBR, GPT, etc), and all filesystems
 * on the disk will be brought online */
OsStatus_t RegisterDisk(UUId_t Driver, UUId_t Device, Flags_t Flags)
{
	/* Variables */
	DiskDescriptor_t Descriptor;

	/* Step 1 - Query disk */
	if (DiskQuery(Device, Driver, &Descriptor) != OsNoError) {
		return OsError;
	}

	/* Step 2 - Build a disk descriptor */


	/* Step 3 - Parse MBR (legacy, gpt etc) */


	/* Step 4 - Start loading file systems for appropriate partitions */


	/* Step 5 - $$$ profit */

	/* Sanity */
	if (!VfsParsePartitionTable(DiskId, 0, Descriptor.SectorCount, Descriptor.SectorSize))
	{
		/* Only one global partition
		* parse FS type from it */

		/* Allocate a filesystem structure */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)kmalloc(sizeof(MCoreFileSystem_t));
		Fs->State = VfsStateInit;
		Fs->DiskId = DiskId;
		Fs->ExtendedData = NULL;
		Fs->Flags = 0;
		Fs->Id = GlbFileSystemId;
		Fs->SectorStart = 0;
		Fs->SectorCount = SectorCount;
		Fs->SectorSize = SectorSize;

		/* Now we have to detect the type of filesystem used
		* normally two types is used for full-partition
		* MFS and FAT */
		/* MFS 1 */
		//TODO
		//Module = ModuleFindGeneric(MODULE_FILESYSTEM, FILESYSTEM_MFS);

		/* Load */
//		if (Module != NULL)
//			ModuleLoad(Module, Fs);

		if (Fs->State != VfsStateActive)
			; //FatInit()

		/* Lastly */
		if (Fs->State == VfsStateActive)
			VfsInstallFileSystem(Fs);
		else
			kfree(Fs);
	}
}

/* Unregisters a disk and all registered fs's on disk
 * Close all files currently open */
void VfsUnregisterDisk(UUId_t DiskId, int Forced)
{
	/* Need this for the iteration */
	ListNode_t *lNode;
	DataKey_t Key;

	/* Keep iterating untill no more FS's are present on disk */
	Key.Value = DiskId;
	lNode = ListGetNodeByKey(GlbFileSystems, Key, 0);

	while (lNode != NULL)
	{
		/* Cast */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)lNode->Data;

		/* Redirect system log */
		if (Fs->Flags & VFS_MAIN_DRIVE) {
			LogRedirect(LogConsole);
		}

		/* Destruct the FS */
		if (Fs->Destroy(lNode->Data, Forced) != OsNoError)
			LogFatal("VFSM", "UnregisterDisk:: Failed to destroy filesystem");

		/* Remove it from list */
		ListRemoveByNode(GlbFileSystems, lNode);

		/* Free */
		MStringDestroy(Fs->Identifier);
		kfree(Fs);
		kfree(lNode);

		/* Get next */
		lNode = ListGetNodeByKey(GlbFileSystems, Key, 0);
	}
}
