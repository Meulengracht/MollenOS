/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - Virtual FileSystem
*/

/* Includes */
#include <Modules/ModuleManager.h>
#include <ProcessManager.h>
#include <Vfs/Vfs.h>
#include <Heap.h>
#include <List.h>
#include <string.h>
#include <Log.h>

/* Globals */
list_t *GlbFileSystems = NULL;
list_t *GlbOpenFiles = NULL;
uint32_t GlbFileSystemId = 0;
uint32_t GlbVfsInitHasRun = 0;

/* Initialize Vfs */
void VfsInit(void)
{
	/* Debug */
	LogInformation("VFSM", "Initializing");

	/* Create lists */
	GlbFileSystems = list_create(LIST_SAFE);
	GlbOpenFiles = list_create(LIST_SAFE);
	GlbFileSystemId = 0;
	GlbVfsInitHasRun = 0;
}

/* Register fs */
void VfsInstallFileSystem(MCoreFileSystem_t *Fs)
{
	/* Ready the buffer */
	char IdentBuffer[8];
	memset(IdentBuffer, 0, 8);

	/* Copy the storage ident over */
	strcpy(IdentBuffer, "St");
	itoa(GlbFileSystemId, (IdentBuffer + 2), 10);

	/* Construct the identifier */
	Fs->Identifier = MStringCreate(&IdentBuffer, StrASCII);

	/* Setup last */
	Fs->Lock = MutexCreate();

	/* Add to list */
	list_append(GlbFileSystems, list_create_node(Fs->DiskId, Fs));

	/* Increament */
	GlbFileSystemId++;

	/* Start init? */
	if (Fs->Flags & VFS_MAIN_DRIVE
		&& !GlbVfsInitHasRun)
	{
		/* Process Request */
		MCoreProcessRequest_t *ProcRequest
			= (MCoreProcessRequest_t*)kmalloc(sizeof(MCoreProcessRequest_t));

		/* Print */
		LogInformation("VFSM", "Boot Drive Detected, Running Init");

		/* Append init path */
		MString_t *Path = MStringCreate(Fs->Identifier->Data, StrUTF8);
		MStringAppendChars(Path, FILESYSTEM_INIT);

		/* Create Request */
		ProcRequest->Type = ProcessSpawn;
		ProcRequest->Path = Path;
		ProcRequest->Arguments = NULL;
		ProcRequest->Cleanup = 1;

		/* Send */
		PmCreateRequest(ProcRequest);

		/* Set */
		GlbVfsInitHasRun = 1;
	}
}

/* Partition table parser */
int VfsParsePartitionTable(DevId_t DiskId, uint64_t SectorBase, uint64_t SectorCount, uint32_t SectorSize)
{
	/* Allocate structures */
	void *TmpBuffer = (void*)kmalloc(SectorSize);
	MCoreModule_t *Module = NULL;
	MCoreMasterBootRecord_t *Mbr = NULL;
	MCoreDeviceRequest_t Request;
	int PartitionCount = 0;
	int i;

	/* Read sector */
	Request.Type = RequestRead;
	Request.DeviceId = DiskId;
	Request.SectorLBA = SectorBase;
	Request.Buffer = (uint8_t*)TmpBuffer;
	Request.Length = SectorSize;

	/* Create & Wait */
	DmCreateRequest(&Request);
	DmWaitRequest(&Request);

	/* Sanity */
	if (Request.Status != RequestOk)
	{
		/* Error */
		LogFatal("VFSM", "REGISTERDISK: Error reading from disk - 0x%x\n", Request.Status);
		kfree(TmpBuffer);
		return 0;
	}

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
			Fs->FsData = NULL;
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
				Module = ModuleFindGeneric(MODULE_FILESYSTEM, FILESYSTEM_MFS);

				/* Load */
				if (Module != NULL)
					ModuleLoad(Module, Fs);
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

/* Registers a disk with the VFS 
 * and parses all possible partiions */
void VfsRegisterDisk(DevId_t DiskId)
{
	/* Query for disk stats */
	MCoreModule_t *Module = NULL;
	char TmpBuffer[20];
	MCoreDeviceRequest_t Request;
	Request.Type = RequestQuery;
	Request.DeviceId = DiskId;
	Request.Buffer = (uint8_t*)TmpBuffer;
	Request.Length = 20;

	/* Memset */
	memset(TmpBuffer, 0, sizeof(TmpBuffer));

	/* Perform */
	DmCreateRequest(&Request);
	DmWaitRequest(&Request);

	/* Well, well */
	uint64_t SectorCount = *(uint64_t*)&TmpBuffer[0];
	uint32_t SectorSize = *(uint32_t*)&TmpBuffer[16];

	/* Sanity */
	if (!VfsParsePartitionTable(DiskId, 0, SectorCount, SectorSize))
	{
		/* Only one global partition 
		 * parse FS type from it */

		/* Allocate a filesystem structure */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)kmalloc(sizeof(MCoreFileSystem_t));
		Fs->State = VfsStateInit;
		Fs->DiskId = DiskId;
		Fs->FsData = NULL;
		Fs->Flags = 0;
		Fs->Id = GlbFileSystemId;
		Fs->SectorStart = 0;
		Fs->SectorCount = SectorCount;
		Fs->SectorSize = SectorSize;

		/* Now we have to detect the type of filesystem used
		 * normally two types is used for full-partition 
		 * MFS and FAT */
		/* MFS 1 */
		Module = ModuleFindGeneric(MODULE_FILESYSTEM, FILESYSTEM_MFS);

		/* Load */
		if (Module != NULL)
			ModuleLoad(Module, Fs);

		if (Fs->State != VfsStateActive)
			; //FatInit()

		/* Lastly */
		if (Fs->State == VfsStateActive)
			VfsInstallFileSystem(Fs);
		else
			kfree(Fs);
	}
}

/* Unregisters a disk and all registered fs's 
 * on disk TODO
 * Close all files currently open */
void VfsUnregisterDisk(DevId_t DiskId, uint32_t Forced)
{
	/* Need this for the iteration */
	list_node_t *lNode;

	/* Keep iterating untill no more FS's are present on disk */
	lNode = list_get_node_by_id(GlbFileSystems, DiskId, 0);

	while (lNode != NULL)
	{
		/* Remove it from list */
		list_remove_by_node(GlbFileSystems, lNode);

		/* Cast */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)lNode->data;

		/* Destruct the FS */
		if (Fs->Destory(lNode->data, Forced) != OsOk)
			LogFatal("VFSM", "UnregisterDisk:: Failed to destroy filesystem");

		/* Free */
		MStringDestroy(Fs->Identifier);
		MutexDestruct(Fs->Lock);
		kfree(Fs);
		kfree(lNode);

		/* Get next */
		lNode = list_get_node_by_id(GlbFileSystems, DiskId, 0);
	}
}

/* Vfs - Canonicalize Path 
 * @Path - UTF-8 String */
MString_t *VfsCanonicalizePath(const char *Path)
{
	/* Store result */
	MString_t *AbsPath = MStringCreate(NULL, StrUTF8);
	uint32_t Itr = 0;

	/* Get working directory */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreThread_t *cThread = ThreadingGetCurrentThread(CurrentCpu);
	MString_t *Cwd = PmGetWorkingDirectory(cThread->ProcessId);

	/* Start by copying cwd over 
	 * if Path is not absolute */
	if (strchr(Path, ':') == NULL)
	{
		/* Unless Cwd is null, then we have a problem */
		if (Cwd == NULL)
		{
			/* Fuck */
			MStringDestroy(AbsPath);
			return NULL;
		}
		else {
			/* Start in working directory */
			MStringCopy(AbsPath, Cwd, -1);

			/* Make sure the path ends on a '/' */
			if (MStringGetCharAt(AbsPath, MStringLength(AbsPath) - 1) != '/')
				MStringAppendChar(AbsPath, '/');
		}
	}
	
	/* Now, we have to resolve the path in Path */
	while (Path[Itr])
	{
		/* Sanity */
		if (Path[Itr] == '/'
			&& Itr == 0)
		{
			Itr++;
			continue;
		}

		/* What char is it ? */
		if (Path[Itr] == '.'
			&& (Path[Itr + 1] == '/' || Path[Itr + 1] == '\\'))
		{
			/* Ignore */
			Itr += 2;
			continue;
		}
		else if (Path[Itr] == '.'
			&& Path[Itr + 1] == '.')
		{
			/* Go one directory back, if we are in one */
			int Index = MStringFindReverse(AbsPath, '/');
			if (MStringGetCharAt(AbsPath, Index - 1) != ':')
			{
				/* Build a new string */
				MString_t *NewAbs = MStringSubString(AbsPath, 0, Index);
				MStringDestroy(AbsPath);
				AbsPath = NewAbs;
			}
		}
		else
		{
			/* Copy over */
			if (Path[Itr] == '\\')
				MStringAppendChar(AbsPath, '/');
			else
				MStringAppendChar(AbsPath, Path[Itr]);
		}

		/* Increase */
		Itr++;
	}

	/* Replace dublicate // with / */
	//MStringReplace(AbsPath, "//", "/");

	/* Done! */
	return AbsPath;
}

/* Vfs - Open File
* @Path - UTF-8 String
* @OpenFlags - Kind of Access */
MCoreFile_t *VfsOpen(const char *Path, VfsFileFlags_t OpenFlags)
{
	/* Vars */
	MCoreFile_t *fRet = NULL;
	list_node_t *fNode = NULL;
	MString_t *mPath = NULL;
	MString_t *mIdent = NULL;
	MString_t *mSubPath = NULL;
	int Index = 0;

	/* Allocate */
	fRet = (MCoreFile_t*)kmalloc(sizeof(MCoreFile_t));

	/* Zero it */
	memset((void*)fRet, 0, sizeof(MCoreFile_t));

	fRet->Code = VfsOk;

	/* Sanity */
	if (Path == NULL)
	{
		fRet->Code = VfsInvalidParameters;
		return fRet;
	}

	/* Canonicalize Path */
	mPath = VfsCanonicalizePath(Path);

	/* Sanity */
	if (mPath == NULL)
	{
		fRet->Code = VfsInvalidPath;
		return fRet;
	}

	/* Get filesystem ident & sub-path */
	Index = MStringFind(mPath, ':');
	mIdent = MStringSubString(mPath, 0, Index);
	mSubPath = MStringSubString(mPath, Index + 2, -1);

	/* Iterate */
	_foreach(fNode, GlbFileSystems)
	{
		/* Cast */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)fNode->data;

		/* Match? */
		if (MStringCompare(mIdent, Fs->Identifier, 1))
		{
			/* Open */
			fRet->Code = Fs->OpenFile(Fs, fRet, mSubPath, OpenFlags);

			/* Sanity */
			if (fRet->Code == VfsOk)
			{
				/* Set stuff */
				fRet->Code = VfsOk;
				fRet->Flags = OpenFlags;
				fRet->Fs = Fs;

				/* Append? */
				if (OpenFlags & Append)
					fRet->Code = Fs->Seek(Fs, fRet, fRet->Size);
			}

			/* Done */
			break;
		}
	}

	/* Cleanup */
	MStringDestroy(mSubPath);
	MStringDestroy(mIdent);
	MStringDestroy(mPath);

	/* Damn */
	return fRet;
}

/* Vfs - Close File
* @Handle - A valid file handle */
VfsErrorCode_t VfsClose(MCoreFile_t *Handle)
{
	/* Vars */
	VfsErrorCode_t ErrCode = VfsPathNotFound;
	MCoreFileSystem_t *Fs = NULL;

	/* Sanity */
	if (Handle == NULL)
		return VfsInvalidParameters;

	/* Invalid Handle? */
	if (Handle->Code != VfsOk
		|| Handle->Fs == NULL)
	{
		kfree(Handle);
		return VfsOk;
	}

	/* Deep Close */
	Fs = (MCoreFileSystem_t*)Handle->Fs;
	ErrCode = Fs->CloseFile(Handle->Fs, Handle);

	/* Cleanup */
	kfree(Handle);

	/* Damn */
	return ErrCode;
}

/* Vfs - Delete File
* @Handle - A valid file handle */
VfsErrorCode_t VfsDelete(MCoreFile_t *Handle)
{
	/* Vars */
	VfsErrorCode_t ErrCode = VfsPathNotFound;
	MCoreFileSystem_t *Fs = NULL;

	/* Sanity */
	if (Handle == NULL
		|| Handle->Code != VfsOk)
		return VfsInvalidParameters;

	/* Deep Delete */
	Fs = (MCoreFileSystem_t*)Handle->Fs;
	ErrCode = Fs->DeleteFile(Handle->Fs, Handle);

	/* Sanity */
	if (ErrCode != VfsOk)
		return ErrCode;

	/* Done */
	Handle->Code = VfsDeleted;
	return VfsClose(Handle);
}

/* Vfs - Read File
* @Handle - A valid file handle
* @Buffer - A valid data buffer
* @Length - How many bytes of data to read */
size_t VfsRead(MCoreFile_t *Handle, uint8_t *Buffer, size_t Length)
{
	/* Vars */
	size_t BytesRead = 0;
	MCoreFileSystem_t *Fs = NULL;

	/* Sanity */
	if (Handle == NULL
		|| Handle->Code != VfsOk)
		return VfsInvalidParameters;

	/* Deep Read */
	Fs = (MCoreFileSystem_t*)Handle->Fs;
	BytesRead = Fs->ReadFile(Fs, Handle, Buffer, Length);

	/* Done */
	return BytesRead;
}

/* Vfs - Write File
* @Handle - A valid file handle
* @Buffer - A valid data buffer
* @Length - How many bytes of data to write */
size_t VfsWrite(MCoreFile_t *Handle, uint8_t *Buffer, size_t Length)
{
	/* Vars */
	size_t BytesWritten = 0;
	MCoreFileSystem_t *Fs = NULL;

	/* Sanity */
	if (Handle == NULL
		|| Handle->Code != VfsOk)
		return VfsInvalidParameters;

	/* Deep Read */
	Fs = (MCoreFileSystem_t*)Handle->Fs;
	BytesWritten = Fs->WriteFile(Fs, Handle, Buffer, Length);

	/* Done */
	return BytesWritten;
}

/* Vfs - Seek in File
* @Handle - A valid file handle
* @Offset - A valid file offset */
VfsErrorCode_t VfsSeek(MCoreFile_t *Handle, uint64_t Offset)
{
	/* Vars */
	VfsErrorCode_t ErrCode = VfsPathNotFound;
	MCoreFileSystem_t *Fs = NULL;

	/* Sanity */
	if (Handle == NULL
		|| Handle->Code != VfsOk)
		return VfsInvalidParameters;

	/* Deep Seek */
	Fs = (MCoreFileSystem_t*)Handle->Fs;
	ErrCode = Fs->Seek(Fs, Handle, (Handle->Position + Offset));

	/* Done */
	return ErrCode;
}

/* Vfs - Query Handle 
 * @Handle - A valid file handle 
 * @Function - The query function 
 * @Buffer - Where to store query results - vAddr
 * @Length - Max length of data to store */
VfsErrorCode_t VfsQuery(MCoreFile_t *Handle, VfsQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Vars */
	VfsErrorCode_t ErrCode = VfsOk;
	MCoreFileSystem_t *Fs = NULL;

	/* Sanity */
	if (Handle == NULL
		|| Handle->Code != VfsOk)
		return VfsInvalidParameters;

	/* Handle VFS Queries that has no further need
	 * for the underlying fs */
	switch (Function) {

		/* Handle Get Access mode */
		case QueryGetAccess: {

			/* Sanity buffer size */
			if (Length < sizeof(int))
				return VfsInvalidParameters;

			/* Store it into the buffer */
			*((int*)Buffer) = (int)Handle->Flags;

		} break;

		/* Handle Set Access mode */
		case QuerySetAccess: {

			/* Sanity buffer size */
			if (Length < sizeof(int))
				return VfsInvalidParameters;

			/* Probably validate requested access flags .. */

			/* Update access mode */
			Handle->Flags = (VfsFileFlags_t)(*((int*)Buffer));

		} break;

		/* Redirect */
		default: {
			/* Deep Query */
			Fs = (MCoreFileSystem_t*)Handle->Fs;
			ErrCode = Fs->Query(Fs, Handle, Function, Buffer, Length);

		} break;
	}

	/* Done */
	return ErrCode;
}

/* Flush, Rename */