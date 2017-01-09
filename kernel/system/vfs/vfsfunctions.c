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
* - Main VFS functions for operations
*/

/* Includes 
 * - System */
#include <Vfs/Vfs.h>
#include <Heap.h>
#include <Log.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <string.h>
#include <ctype.h>

/* Externs 
 * - We import from VFS */
extern List_t *GlbFileSystems;
extern List_t *GlbOpenFiles;
extern int GlbVfsFileIdGen;

/* Externs 
 * - Prototypes */

/* Vfs - Canonicalize Path
* @Path - UTF-8 String */
extern MString_t *VfsCanonicalizePath(VfsEnvironmentPath_t Base, const char *Path);

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
		BytesWritten = Fs->WriteFile(Fs, Handle, Handle->oBuffer, Handle->oBufferPosition);

		/* Sanity */
		if (BytesWritten != Handle->oBufferPosition)
			return VfsDiskError;
	}

	/* Done */
	return VfsOk;
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
		Instance->Code = Fs->SeekFile(Fs, Instance, fHandle->Size);

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
	ListNode_t *fNode = NULL;
	MString_t *mIdent = NULL;
	DataKey_t Key;
	int Index = 0;

	/* Check Cache */
	size_t PathHash = MStringHash(Path);
	Key.Value = (int)PathHash;
	ListNode_t *pNode = ListGetNodeByKey(GlbOpenFiles, Key, 0);

	/* Did it exist? */
	if (pNode != NULL)
	{
		/* Get file-entry */
		MCoreFile_t *fEntry = (MCoreFile_t*)pNode->Data;

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
		MCoreFileSystem_t *Fs = (MCoreFileSystem_t*)fNode->Data;

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

				/* Copy path */
				fHandle->Path = MStringCreate((void*)MStringRaw(Path), StrUTF8);

				/* Create handle */
				VfsOpenHandleInternal(Fs, Instance, fHandle, OpenFlags);

				/* Add to list */
				ListAppend(GlbOpenFiles, ListCreateNode(Key, Key, fHandle));
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
	fRet->Id = GlbVfsFileIdGen++;
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
			if ((fRet->Code == VfsInvalidPath
				|| fRet->Code == VfsPathNotFound
				|| fRet->Code == VfsPathIsNotDirectory)
				&& i < ((int)PathEnvironmentCount - 1)) {
				/* Reset, Continue */
				memset((void*)fRet, 0, sizeof(MCoreFileInstance_t));
				fRet->Code = VfsPathNotFound;
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
	DataKey_t Key;

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

	/* Cast key */
	Key.Value = (int)Handle->File->Hash;

	/* Last reference? */
	if (Handle->File->References <= 0) 
	{
		/* Find node in open files */
		ListNode_t *pNode = ListGetNodeByKey(GlbOpenFiles, Key, 0);

		/* Deep Close */
		ErrCode = Fs->CloseFile(Fs, Handle->File);
		MStringDestroy(Handle->File->Path);
		kfree(Handle->File);

		/* Remove from list */
		if (pNode != NULL) {
			ListRemoveByNode(GlbOpenFiles, pNode);
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
	if (Length == 0)
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
	BytesRead = Fs->ReadFile(Fs, Handle, Buffer, Length);

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

	/* Security Sanity */
	if (!(Handle->Flags & Write))
		return VfsAccessDenied;

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
				BytesWritten = Fs->WriteFile(Fs, Handle, TempBuffer, 
					Handle->oBufferPosition + Length);

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
		BytesWritten = Fs->WriteFile(Fs, Handle, Buffer, Length);

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
	ErrCode = Fs->SeekFile(Fs, Handle, Offset);

	/* Clear last op */
	Handle->LastOp = 0;
	Handle->IsEOF = 0;

	/* Done */
	return ErrCode;
}

/* Vfs - Query Handle 
 * @Handle - A valid file handle 
 * @Function - The query function 
 * @Buffer - Where to store query results - vAddr
 * @Length - Max length of data to store */
VfsErrorCode_t VfsQuery(MCoreFileInstance_t *Handle, 
	VfsQueryFunction_t Function, void *Buffer, size_t Length)
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
			ErrCode = Fs->Query(Fs, Handle, Function, Buffer, Length);

		} break;
	}

	/* Done */
	return ErrCode;
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
