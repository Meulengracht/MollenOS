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
#include <Log.h>

/* CLib */
#include <string.h>
#include <ctype.h>

/* Globals */
list_t *GlbFileSystems = NULL;
list_t *GlbOpenFiles = NULL;
uint32_t GlbFileSystemId = 0;
uint32_t GlbVfsInitHasRun = 0;

/* Environment String Array */
const char *GlbEnvironmentalPaths[] = {
	"N/A",
	
	"N/A",
	":/Shared/AppData/"

	":/",
	":/System/",

	":/Shared/Bin/",
	":/Shared/Documents/",
	":/Shared/Includes/",
	":/Shared/Libraries/",
	":/Shared/Media/",

	":/Users/"
};

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

/* String Hash */
size_t VfsHash(uint8_t *Str)
{
	/* Hash Seed */
	size_t Hash = 5381;
	int Char;

	/* Hash */
	while ((Char = tolower(*Str++)) != 0)
		Hash = ((Hash << 5) + Hash) + Char; /* hash * 33 + c */

	/* Done */
	return Hash;
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
		if (Fs->Destroy(lNode->data, Forced) != OsOk)
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

/* Vfs - Resolve Environmental Path
 * @Base - Environmental Path */
MString_t *VfsResolveEnvironmentPath(VfsEnvironmentPath_t Base)
{
	/* Handle Special Case - 0 & 1
	 * Just return the current working directory */
	if (Base == PathCurrentWorkingDir
		|| Base == PathApplicationBase) 
	{
		/* Get current process */
		Cpu_t CurrentCpu = ApicGetCpu();
		MCoreThread_t *cThread = ThreadingGetCurrentThread(CurrentCpu);

		if (Base == PathCurrentWorkingDir)
			return PmGetWorkingDirectory(cThread->ProcessId);
		else
			return PmGetBaseDirectory(cThread->ProcessId);
	}

	/* Otherwise we have to lookup in a string table */
	MString_t *ResolvedPath = MStringCreate(NULL, StrUTF8);
	list_node_t *fNode = NULL;
	int pIndex = (int)Base;
	int pFound = 0;

	/* Get system path */
	_foreach(fNode, GlbFileSystems)
	{
		/* Cast */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)fNode->data;

		/* Boot drive? */
		if (Fs->Flags & VFS_MAIN_DRIVE) {
			MStringAppendString(ResolvedPath, Fs->Identifier);
			pFound = 1;
			break;
		}
	}

	/* Sanity */
	if (!pFound) {
		MStringDestroy(ResolvedPath);
		return NULL;
	}

	/* Now append the special paths */
	MStringAppendChars(ResolvedPath, GlbEnvironmentalPaths[pIndex]);

	/* Done! */
	return ResolvedPath;
}

/* Vfs - Canonicalize Path 
 * @Path - UTF-8 String */
MString_t *VfsCanonicalizePath(VfsEnvironmentPath_t Base, const char *Path)
{
	/* Store result */
	MString_t *AbsPath = MStringCreate(NULL, StrUTF8);
	list_node_t *fNode = NULL;
	uint32_t Itr = 0;

	/* Get base directory */
	MString_t *BasePath = VfsResolveEnvironmentPath(Base);

	/* Start by copying cwd over 
	 * if Path is not absolute or specifier */
	if (strchr(Path, ':') == NULL
		&& strchr(Path, '%') == NULL)
	{
		/* Unless Base is null, then we have a problem */
		if (BasePath == NULL)
		{
			/* Fuck */
			MStringDestroy(AbsPath);
			return NULL;
		}
		else {
			/* Start in working directory */
			MStringCopy(AbsPath, BasePath, -1);

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

		/* Identifier/Keyword? */
		if (Path[Itr] == '%') 
		{
			/* Which kind of keyword? */
			if ((Path[Itr + 1] == 'S' || Path[Itr + 1] == 's')
				&& Path[Itr + 2] == 'y'
				&& Path[Itr + 3] == 's'
				&& Path[Itr + 4] == '%') 
			{
				/* Get boot drive identifer */
				_foreach(fNode, GlbFileSystems)
				{
					/* Cast */
					MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)fNode->data;

					/* Boot drive? */
					if (Fs->Flags & VFS_MAIN_DRIVE) {
						MStringAppendString(AbsPath, Fs->Identifier);
						break;
					}
				}

				/* Skip */
				Itr += 5;
				continue;
			}
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

/* Vfs - Reusable helper for the VfsOpen
 * @Fs - The relevant filesystem
 * @Instance - A pre-allocated handle that describes a file instance
 * @fHandle - An pre-allocated handle that describes a file
 * @OpenFlags - Open mode of file */
void VfsOpenHandleInternal(MCoreFileSystem_t *Fs, 
	MCoreFileInstance_t *Instance, MCoreFile_t *fHandle, VfsFileFlags_t OpenFlags)
{
	/* Create handle */
	Fs->OpenHandle(Fs, fHandle, Instance);

	/* Set stuff */
	Instance->Code = VfsOk;
	Instance->Flags = OpenFlags;
	Instance->LastOp = 0;
	Instance->File = fHandle;

	/* Initialise buffering */
	if (!(Instance->Flags & NoBuffering))
	{
		/* Setup output buffer */
		Instance->oBuffer = (void*)kmalloc(Fs->SectorSize);
		memset(Instance->oBuffer, 0, Fs->SectorSize);
		Instance->oBufferPosition = 0;
	}

	/* Append? */
	if (OpenFlags & Append)
		Instance->Code = Fs->Seek(Fs, fHandle, Instance, fHandle->Size);

	/* File locked? */
	if (OpenFlags & Write)
		fHandle->IsLocked = 1;
}

/* Vfs - Reusable helper for the VfsOpen
* @Handle - An pre-allocated handle
* @Path - The path to try to open */
void VfsOpenInternal(MCoreFileInstance_t *Instance, MString_t *Path, VfsFileFlags_t OpenFlags)
{
	/* Variables needed */
	MString_t *mSubPath = NULL;
	list_node_t *fNode = NULL;
	MString_t *mIdent = NULL;
	int Index = 0;

	/* Check Cache */
	size_t PathHash = VfsHash(Path->Data);
	list_node_t *pNode = list_get_node_by_id(GlbOpenFiles, (int)PathHash, 0);

	/* Did it exist? */
	if (pNode != NULL)
	{
		/* Get file-entry */
		MCoreFile_t *fEntry = (MCoreFile_t*)pNode->data;

		/* If file is locked, bad luck */
		if (fEntry->IsLocked) {
			Instance->Code = VfsAccessDenied;
			return;
		}

		/* Ok ok, create a new handle
		* since file is opened and exists */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)fEntry->Fs;

		/* Create handle */
		VfsOpenHandleInternal(Fs, Instance, fEntry, OpenFlags);

		/* Increase fEntry ref count */
		fEntry->References++;

		/* Done */
		return;
	}

	/* Get filesystem ident & sub-path */
	Index = MStringFind(Path, ':');
	mIdent = MStringSubString(Path, 0, Index);
	mSubPath = MStringSubString(Path, Index + 2, -1);

	/* Iterate */
	_foreach(fNode, GlbFileSystems)
	{
		/* Cast */
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)fNode->data;

		/* Match? */
		if (MStringCompare(mIdent, Fs->Identifier, 1))
		{
			/* Allocate a new handle */
			MCoreFile_t *fHandle = (MCoreFile_t*)kmalloc(sizeof(MCoreFile_t));
			memset(fHandle, 0, sizeof(MCoreFile_t));

			/* Open */
			Instance->Code = Fs->OpenFile(Fs, fHandle, mSubPath, OpenFlags);

			/* Sanity */
			if (Instance->Code == VfsOk)
			{
				/* Set initial stuff */
				fHandle->Fs = Fs;
				fHandle->References = 1;
				fHandle->Hash = PathHash;

				/* Create handle */
				VfsOpenHandleInternal(Fs, Instance, fHandle, OpenFlags);

				/* Add to list */
				list_append(GlbOpenFiles, list_create_node((int)PathHash, fHandle));
			}

			/* Done */
			break;
		}
	}

	/* Cleanup */
	MStringDestroy(mSubPath);
	MStringDestroy(mIdent);
}

/* Vfs - Open File
* @Path - UTF-8 String
* @OpenFlags - Kind of Access */
MCoreFileInstance_t *VfsOpen(const char *Path, VfsFileFlags_t OpenFlags)
{
	/* Vars */
	MCoreFileInstance_t *fRet = NULL;
	MString_t *mPath = NULL;
	int i = 0;

	/* Allocate */
	fRet = (MCoreFileInstance_t*)kmalloc(sizeof(MCoreFileInstance_t));
	memset((void*)fRet, 0, sizeof(MCoreFileInstance_t));

	/* Set initial code */
	fRet->Code = VfsOk;

	/* Sanity */
	if (Path == NULL) {
		fRet->Code = VfsInvalidParameters;
		return fRet;
	}

	/* If path is not absolute or special, we 
	 * must try all 'relative' possble paths... */
	if (strchr(Path, ':') == NULL
		&& strchr(Path, '%') == NULL) 
	{
		/* Now we loop through all possible locations
		* of %PATH% */
		for (i = 0; i < (int)PathEnvironmentCount; i++)
		{
			/* Which locations do we allow? */
			if (i != (int)PathCurrentWorkingDir
				&& i != (int)PathSystemDirectory
				&& i != (int)PathCommonBin)
				continue;

			/* Canonicalize Path */
			mPath = VfsCanonicalizePath((VfsEnvironmentPath_t)i, Path);

			/* Sanity */
			if (mPath == NULL) {
				fRet->Code = VfsInvalidPath;
				continue;
			}

			/* Try to open */
			VfsOpenInternal(fRet, mPath, OpenFlags);

			/* Cleanup path */
			MStringDestroy(mPath);

			/* Sanity */
			if (fRet->Code == VfsInvalidPath
				|| fRet->Code == VfsPathNotFound
				|| fRet->Code == VfsPathIsNotDirectory) {
				/* Reset, Continue */
				memset((void*)fRet, 0, sizeof(MCoreFileInstance_t));
			}
			else
				break;
		}
	}
	else
	{
		/* Handle it like a normal path 
		 * since we gave an absolute */

		/* Canonicalize Path */
		mPath = VfsCanonicalizePath(PathCurrentWorkingDir, Path);

		/* Sanity */
		if (mPath == NULL) {
			fRet->Code = VfsInvalidPath;
			return fRet;
		}

		/* Try to open */
		VfsOpenInternal(fRet, mPath, OpenFlags);

		/* Cleanup path */
		MStringDestroy(mPath);
	}

	/* Damn */
	return fRet;
}

/* Vfs - Close File
* @Handle - A valid file handle */
VfsErrorCode_t VfsClose(MCoreFileInstance_t *Handle)
{
	/* Vars */
	VfsErrorCode_t ErrCode = VfsPathNotFound;
	MCoreFileSystem_t *Fs = NULL;

	/* Sanity */
	if (Handle == NULL)
		return VfsInvalidParameters;

	/* Cleanup Buffers */
	if (!(Handle->Flags & NoBuffering)) 
	{
		/* Flush them first */
		VfsFlush(Handle);
		
		/* Cleanup */
		if (Handle->oBuffer != NULL)
			kfree(Handle->oBuffer);
	}

	/* Invalid Handle? */
	if (Handle->File == NULL
		|| Handle->File->Fs == NULL) {
		kfree(Handle);
		return VfsOk;
	}

	/* Cast */
	Fs = (MCoreFileSystem_t*)Handle->File->Fs;

	/* Close handle */
	Fs->CloseHandle(Fs, Handle);

	/* Reduce Ref count */
	Handle->File->References--;

	/* Last reference? */
	if (Handle->File->References <= 0) 
	{
		/* Find node in open files */
		list_node_t *pNode = list_get_node_by_id(GlbOpenFiles, (int)Handle->File->Hash, 0);

		/* Deep Close */
		ErrCode = Fs->CloseFile(Fs, Handle->File);
		kfree(Handle->File);

		/* Remove from list */
		if (pNode != NULL) {
			list_remove_by_node(GlbOpenFiles, pNode);
			kfree(pNode);
		}
	}

	/* Cleanup */
	kfree(Handle);

	/* Damn */
	return ErrCode;
}

/* Vfs - Delete File
* @Handle - A valid file handle */
VfsErrorCode_t VfsDelete(MCoreFileInstance_t *Handle)
{
	/* Vars */
	VfsErrorCode_t ErrCode = VfsPathNotFound;
	MCoreFileSystem_t *Fs = NULL;

	/* Sanity */
	if (Handle == NULL
		|| Handle->Code != VfsOk)
		return VfsInvalidParameters;

	/* Deep Delete */
	Fs = (MCoreFileSystem_t*)Handle->File->Fs;
	ErrCode = Fs->DeleteFile(Fs, Handle->File);

	/* Sanity */
	if (ErrCode != VfsOk)
		return ErrCode;

	/* Done */
	Handle->Code = VfsDeleted;

	/* Cleanup */
	return VfsClose(Handle);
}

/* Vfs - Read File
* @Handle - A valid file handle
* @Buffer - A valid data buffer
* @Length - How many bytes of data to read */
size_t VfsRead(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length)
{
	/* Vars */
	MCoreFileSystem_t *Fs = NULL;
	size_t BytesRead = 0;

	/* Sanity */
	if (Handle == NULL
		|| Handle->Code != VfsOk)
		return VfsInvalidParameters;

	/* Sanity */
	if (Handle->IsEOF
		|| Length == 0)
		return 0;

	/* EOF Sanity */
	if (Handle->Position == Handle->File->Size) {
		Handle->IsEOF = 1;
		return 0;
	}

	/* Security Sanity */
	if (!(Handle->Flags & Read)) {
		Handle->Code = VfsAccessDenied;
		return 0;
	}

	/* Sanity -> Flush if we wrote and now read */
	if (Handle->LastOp & Write) {
		VfsFlush(Handle);
	}

	/* Cast */
	Fs = (MCoreFileSystem_t*)Handle->File->Fs;

	/* Deep Read */
	BytesRead = Fs->ReadFile(Fs, Handle->File, Handle, Buffer, Length);

	/* Update Position + Save last op */
	Handle->Position += BytesRead;
	Handle->LastOp = Read;

	/* Done */
	return BytesRead;
}

/* Vfs - Write File
* @Handle - A valid file handle
* @Buffer - A valid data buffer
* @Length - How many bytes of data to write */
size_t VfsWrite(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length)
{
	/* Vars */
	MCoreFileSystem_t *Fs = NULL;
	size_t BytesWritten = 0;
	int WriteToDisk = 0;

	/* Sanity */
	if (Handle == NULL
		|| Handle->Code != VfsOk)
		return VfsInvalidParameters;

	/* Sanity -> Clear read buffer if we are writing */
	if (Handle->LastOp & Read) {
		VfsFlush(Handle);
	}

	/* Get Fs */
	Fs = (MCoreFileSystem_t*)Handle->File->Fs;

	/* Write to buffer if we can */
	if (!(Handle->Flags & NoBuffering))
	{
		/* We have few cases to handle here */
		size_t BytesAvailable = Fs->SectorSize - Handle->oBufferPosition;

		/* Do we have enough room for the entire transaction? */
		if (Length < BytesAvailable) {
			uint8_t *bPtr = (uint8_t*)Handle->oBuffer;
			memcpy((bPtr + Handle->oBufferPosition), Buffer, Length);
			Handle->oBufferPosition += Length;
			BytesWritten = Length;
		}
		else
		{
			/* This is only really neccesary if we actually
			 * used the buffer */
			if (Handle->oBufferPosition != 0)
			{
				/* Allocate a temporary buffer big enough */
				uint8_t *TempBuffer =
					(uint8_t*)kmalloc(Handle->oBufferPosition + Length);

				/* Copy data over */
				memcpy(TempBuffer, Handle->oBuffer, Handle->oBufferPosition);
				memcpy(TempBuffer + Handle->oBufferPosition, Buffer, Length);

				/* Write to Disk */
				BytesWritten = Fs->WriteFile(Fs, Handle->File, Handle,
					TempBuffer, Handle->oBufferPosition + Length);

				/* Sanity */
				if (BytesWritten > Length)
					BytesWritten = Length;

				/* Free temporary buffer */
				kfree(TempBuffer);

				/* Reset index */
				memset(Handle->oBuffer, 0, Fs->SectorSize);
				Handle->oBufferPosition = 0;
			}
			else
				WriteToDisk = 1;
		}
	}
	else
		WriteToDisk = 1;

	/* Deep Write */
	if (WriteToDisk)
		BytesWritten = Fs->WriteFile(Fs, Handle->File, Handle, Buffer, Length);

	/* Save last operation */
	Handle->LastOp = Write;

	/* Done */
	return BytesWritten;
}

/* Vfs - Seek in File
* @Handle - A valid file handle
* @Offset - A valid file offset */
VfsErrorCode_t VfsSeek(MCoreFileInstance_t *Handle, uint64_t Offset)
{
	/* Vars */
	VfsErrorCode_t ErrCode = VfsPathNotFound;
	MCoreFileSystem_t *Fs = NULL;

	/* Sanity */
	if (Handle == NULL
		|| Handle->Code != VfsOk)
		return VfsInvalidParameters;

	/* Flush buffers before seeking */
	VfsFlush(Handle);

	/* Deep Seek */
	Fs = (MCoreFileSystem_t*)Handle->File->Fs;
	ErrCode = Fs->Seek(Fs, Handle->File, Handle, Offset);

	/* Clear last op */
	Handle->LastOp = 0;

	/* Done */
	return ErrCode;
}

/* Vfs - Query Handle 
 * @Handle - A valid file handle 
 * @Function - The query function 
 * @Buffer - Where to store query results - vAddr
 * @Length - Max length of data to store */
VfsErrorCode_t VfsQuery(MCoreFileInstance_t *Handle, VfsQueryFunction_t Function, void *Buffer, size_t Length)
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
			Fs = (MCoreFileSystem_t*)Handle->File->Fs;
			ErrCode = Fs->Query(Fs, Handle->File, Handle, Function, Buffer, Length);

		} break;
	}

	/* Done */
	return ErrCode;
}

/* Vfs - Flush Handle
* @Handle - A valid file handle */
VfsErrorCode_t VfsFlush(MCoreFileInstance_t *Handle)
{
	/* Vars */
	MCoreFileSystem_t *Fs = NULL;
	size_t BytesWritten = 0;

	/* Sanity */
	if (Handle == NULL)
		return VfsInvalidParameters;

	/* Sanity */
	if ((Handle->Flags & NoBuffering)
		|| Handle->LastOp == 0)
		return VfsOk;

	/* Cast */
	Fs = (MCoreFileSystem_t*)Handle->File->Fs;

	/* Empty output buffer */
	if (Handle->oBuffer != NULL
		&& Handle->oBufferPosition != 0) 
	{
		/* Write Buffer */
		BytesWritten = Fs->WriteFile(Fs, Handle->File, Handle, Handle->oBuffer, Handle->oBufferPosition);

		/* Sanity */
		if (BytesWritten != Handle->oBufferPosition)
			return VfsDiskError;
	}

	/* Done */
	return VfsOk;
}

/* Vfs - Move/Rename File
 * @Path - A valid file path
 * @NewPath - A valid file destination
 * @Copy - Whether or not to move the file or copy it there */
VfsErrorCode_t VfsMove(const char *Path, const char *NewPath, int Copy)
{
	_CRT_UNUSED(Path);
	_CRT_UNUSED(NewPath);
	_CRT_UNUSED(Copy);
	return VfsOk;
}

/* Vfs - Create Directory Path 
 * @Path - A valid file path */
VfsErrorCode_t VfsCreatePath(const char *Path)
{
	_CRT_UNUSED(Path);
	return VfsOk;
}