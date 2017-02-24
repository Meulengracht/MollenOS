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
#include <os/driver/file.h>
#include "include/vfs.h"
#include <os/mollenos.h>
#include <ds/list.h>

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* VfsOpenHandleInternal
 * Internal helper for instantiating the file handle
 * this does not take care of anything else than
 * opening the handle */
FileSystemCode_t 
VfsOpenHandleInternal(
	_Out_ FileSystemFileHandle_t *Handle,
	_In_ FileSystemFile_t *File)
{
	/* Instantiate some pointers */
	FileSystem_t *Fs = (FileSystem_t*)File->System;
	FileSystemCode_t Code = FsOk;

	/* Set some initial variables 
	 * Id, Options, Access, Owner has been set */
	Handle->LastOperation == __FILE_OPERATION_READ;
	Handle->OutBuffer = NULL;
	Handle->OutBufferPosition = 0;
	Handle->Position = 0;
	Handle->File = File;

	/* Instantiate the handle on a per-fs basis */
	Code = Fs->Module->OpenHandle(&Fs->Descriptor, Handle);

	/* Sanitize the code */
	if (Code != FsOk) {
		return Code;
	}

	/* Initialise buffering as long as the file
	 * handle is not opened as volatile */
	if (!(Handle->Options & __FILE_VOLATILE)) {
		Handle->OutBuffer = malloc(Fs->Descriptor.Disk.Descriptor.SectorSize);
		memset(Handle->OutBuffer, 0, Fs->Descriptor.Disk.Descriptor.SectorSize);
	}

	/* Now comes the step where we handle options 
	 * - but only options that are handle-specific */
	if (Handle->Options & __FILE_APPEND) {
		Code = Fs->Module->SeekFile(&Fs->Descriptor, Handle, File->Size);
	}

	/* File locked for access? */
	if (Handle->Access & __FILE_WRITE_ACCESS
		&& !(Handle->Access & __FILE_WRITE_SHARE)) {
		File->IsLocked = Handle->Owner;
	}

	/* Return the code */
	return Code;
}

/* VfsOpenInternal
 * Reusable helper for the VfsOpen to open internal
 * handles and performs the interaction with fs */
FileSystemCode_t 
VfsOpenInternal(
	_Out_ FileSystemFileHandle_t *Handle, 
	_In_ MString_t *Path)
{
	/* Variables */
	ListNode_t *fNode = NULL, *pNode = NULL;
	FileSystemCode_t Code = FsOk;
	MString_t *Identifier = NULL;
	MString_t *SubPath = NULL;
	size_t PathHash = 0;
	DataKey_t Key;
	int Index = 0;

	/* To ensure that we don't spend resources doing
	 * the wheel all over again, compute the hash 
	 * and check cache */
	PathHash = MStringHash(Path);
	Key.Value = (int)PathHash;
	pNode = ListGetNodeByKey(VfsGetOpenFiles(), Key, 0);

	/* Did it exist? */
	if (pNode != NULL) {
		FileSystemFile_t *File = (FileSystemFile_t*)pNode->Data;

		/* If file is locked, bad luck 
		 * - Otherwise open a handle and increase ref-count */
		if (File->IsLocked != UUID_INVALID) {
			return FsAccessDenied;
		}
		else {
			Code = VfsOpenHandleInternal(Handle, File);
			if (Code == FsOk) {
				File->References++;
			}
			return Code;
		}
	}

	/* To open a new file we need to find the correct
	 * filesystem identifier and seperate it from it's absolute path */
	Index = MStringFind(Path, ':');
	Identifier = MStringSubString(Path, 0, Index);
	SubPath = MStringSubString(Path, Index + 2, -1);

	/* Iterate all the filesystems and find the one
	 * that matches */
	_foreach(fNode, VfsGetFileSystems()) {
		FileSystem_t *Fs = (FileSystem_t*)fNode->Data;
		if (MStringCompare(Identifier, Fs->Identifier, 1)) 
		{
			/* We found it, allocate a new file structure and prefill
			 * some information, the module call will fill rest */
			FileSystemFile_t *File = (FileSystemFile_t*)malloc(sizeof(FileSystemFile_t));
			memset(File, 0, sizeof(FileSystemFile_t));
			File->System = (uintptr_t*)&Fs->Descriptor;
			File->Path = MStringCreate((void*)MStringRaw(Path), StrUTF8);
			File->Hash = PathHash;

			/* Let the module do the rest */
			Code = Fs->Module->OpenFile(&Fs->Descriptor, File, SubPath, Handle->Access);

			/* Sanitize the open
			 * otherwise we must cleanup */
			if (Code == FsOk) {
				Code = VfsOpenHandleInternal(Handle, File);
				ListAppend(VfsGetOpenFiles(), ListCreateNode(Key, Key, File));
				File->References = 1;
			}
			else {
				MStringDestroy(File->Path);
				free(File);
			}

			break;
		}
	}

	/* Cleanup */
	MStringDestroy(Identifier);
	MStringDestroy(SubPath);
	return Code;
}

/* OpenFile
 * Opens or creates the given file path based on
 * the given <Access> and <Options> flags. See the
 * top of this file */
FileSystemCode_t
OpenFile(
	_In_ UUId_t Requester,
	_In_ __CONST char *Path, 
	_In_ Flags_t Options, 
	_In_ Flags_t Access,
	_Out_ UUId_t *Handle)
{
	/* Variables */
	FileSystemFileHandle_t *hFile = NULL;
	FileSystemCode_t Code = FsOk;
	MString_t *mPath = NULL;
	DataKey_t Key;
	int i = 0;

	/* Sanitize parameters */
	if (Path == NULL) {
		return FsInvalidParameters;
	}

	/* Allocate a new file-handle instance as we'll need it */
	hFile = (FileSystemFileHandle_t*)malloc(sizeof(FileSystemFileHandle_t));
	memset((void*)hFile, 0, sizeof(FileSystemFileHandle_t));

	/* Initialize to default values */
	hFile->Owner = Requester;
	hFile->Access = Access;
	hFile->Options = Options;

	/* If path is not absolute or special, we 
	 * must try all 'relative' possble paths... */
	if (strchr(Path, ':') == NULL
		&& strchr(Path, '%') == NULL) 
	{
		/* Now we loop through all possible locations
		 * of our hardcoded :( environment paths */
		for (i = 0; i < (int)PathEnvironmentCount; i++)
		{
			/* Which locations do we allow? */
			if (i != (int)PathCurrentWorkingDirectory
				&& i != (int)PathSystemDirectory
				&& i != (int)PathCommonBin) {
				continue;
			}	

			/* Canonicalize Path */
			mPath = PathCanonicalize((EnvironmentPath_t)i, Path);

			/* Sanitize that the path is valid */
			if (mPath == NULL) {
				Code = FsInvalidPath;
				continue;
			}
			else {
				Code = VfsOpenInternal(hFile, mPath);
			}
			
			/* Cleanup path before continuing */
			MStringDestroy(mPath);

			/* Sanitize the status in which we tried to
			 * open the current path */
			if ((Code == FsInvalidPath
				|| Code == FsPathNotFound
				|| Code == FsPathIsNotDirectory)
				&& i < ((int)PathEnvironmentCount - 1)) {
				Code = FsPathNotFound;
			}
			else {
				break;
			}
		}
	}
	else
	{
		/* Handle it like a normal path 
		 * since we gave an absolute - and work from current
		 * working directory */
		mPath = PathCanonicalize(PathCurrentWorkingDirectory, Path);

		/* Sanitize that the path is valid */
		if (mPath == NULL) {
			Code = FsInvalidPath;
		}
		else {
			Code = VfsOpenInternal(hFile, mPath);
		}

		/* Cleanup path */
		MStringDestroy(mPath);
	}

	/* Sanitize code */
	if (Code != FsOk) {
		*Handle = UUID_INVALID;
		free(hFile);
	}
	else {
		*Handle = hFile->Id 
			= VfsIdentifierFileGet();
		Key.Value = (int)hFile->Id;
		ListAppend(VfsGetOpenHandles(),
			ListCreateNode(Key, Key, hFile));
	}

	/* Done - return code */
	return Code;
}

/* CloseFile
 * Closes the given file-handle, but does not necessarily
 * close the link to the file. Returns the result */
FileSystemCode_t
CloseFile(
	_In_ UUId_t Requester, 
	_In_ UUId_t Handle)
{
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	FileSystemCode_t Code = FsOk;
	ListNode_t *hNode = NULL;
	FileSystem_t *Fs = NULL;
	DataKey_t Key;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = ListGetNodeByKey(VfsGetOpenHandles(), Key, 0);

	/* Case 1 - Not found */
	if (hNode == NULL) {
		return FsInvalidParameters;
	}

	/* Instantiate pointer for next check */
	fHandle = (FileSystemFileHandle_t*)hNode->Data;
	
	/* Case 2 - Invalid Owner */
	if (fHandle->Owner != Requester) {
		return FsAccessDenied;
	}

	/* If there has been allocated any buffers they should
	 * be flushed and cleaned up */
	if (!(fHandle->Options & __FILE_VOLATILE)) {
		VfsFlush(Handle);
		free(fHandle->OutBuffer);
	}

	/* Instantiate the filesystem pointer */
	Fs = (FileSystem_t*)fHandle->File->System;

	/* Invoke the close handle to cleanup anything
	 * allocated by the filesystem layer */
	Code = Fs->Module->CloseHandle(&Fs->Descriptor, fHandle);

	/* Take care of any file cleanup / reduction */
	Key.Value = (int)fHandle->File->Hash;
	fHandle->File->References--;
	if (fHandle->File->IsLocked == Requester) {
		fHandle->File->IsLocked == UUID_INVALID;
	}

	/* Last reference?
	 * Cleanup the file in case of no refs */
	if (fHandle->File->References <= 0) {
		ListNode_t *pNode = ListGetNodeByKey(VfsGetOpenFiles(), Key, 0);

		/* Cleanup the file */
		Code = Fs->Module->CloseFile(&Fs->Descriptor, fHandle->File);
		MStringDestroy(fHandle->File->Path);
		free(fHandle->File);

		/* Remove from list */
		if (pNode != NULL) {
			ListRemoveByNode(VfsGetOpenFiles(), pNode);
			kfree(pNode);
		}
	}

	/* Remove the handle from list */
	ListRemoveByNode(VfsGetOpenHandles(), hNode);
	free(hNode);

	/* Cleanup the handle - return code */
	free(fHandle);
	return Code;
}

/* DeleteFile
 * Deletes the given file denoted by the givne path
 * the caller must make sure there is no other references
 * to the file - otherwise delete fails */
FileSystemCode_t
DeleteFile(
	_In_ UUId_t Requester, 
	_In_ __CONST char *Path)
{
	/* Variables */
	FileSystemFileHandle_t *Handle = NULL;
	FileSystemCode_t Code = FsOk;
	UUId_t HandleId = UUID_INVALID;
	ListNode_t *hNode = NULL;
	FileSystem_t *Fs = NULL;
	DataKey_t Key;

	/* Open the file */
	Code = OpenFile(Requester, Path, __FILE_MUSTEXIST | __FILE_VOLATILE,
		__FILE_WRITE_ACCESS, &HandleId);

	/* Sanitize the result */
	if (Code != FsOk) {
		return Code;
	}

	/* Convert the handle id into a handle */
	Key.Value = (int)HandleId;
	hNode = ListGetNodeByKey(VfsGetOpenHandles(), Key, 0);
	Handle = (FileSystemFileHandle_t*)hNode->Data;

	/* Make sure there is only one reference to
	 * the file - otherwise fail */
	if (Handle->File->References > 1) {
		Code = FsAccessDenied;
		goto Cleanup;
	}

	/* Deep Delete */
	Fs = (FileSystem_t*)Handle->File->System;
	Code = Fs->Module->DeleteFile(&Fs->Descriptor, Handle);

Cleanup:
	/* Close and cleanup file 
	 * - Return the code from the delete operation */
	CloseFile(Requester, HandleId);
	return Code;
}

/* ReadFile
 * Reads the requested number of bytes into the given buffer
 * from the current position in the handle filehandle */
FileSystemCode_t
ReadFile(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_Out_ BufferObject_t *BufferObject,
	_Out_ size_t *BytesRead)
{
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	FileSystemCode_t Code = FsOk;
	ListNode_t *hNode = NULL;
	FileSystem_t *Fs = NULL;
	DataKey_t Key;

	/* Sanitize request parameters first
	* Is handle valid? */
	Key.Value = (int)Handle;
	hNode = ListGetNodeByKey(VfsGetOpenHandles(), Key, 0);

	/* Case 1 - Not found / Invalid parameters */
	if (hNode == NULL
		|| BufferObject == NULL
		|| BufferObject->Length == 0) {
		return FsInvalidParameters;
	}

	/* Instantiate pointer for next check */
	fHandle = (FileSystemFileHandle_t*)hNode->Data;

	/* Case 2 - Invalid Owner / Missing Access */
	if (fHandle->Owner != Requester
		|| !(fHandle->Access & __FILE_READ_ACCESS)
		|| (fHandle->File->IsLocked != UUID_INVALID
			&& fHandle->File->IsLocked != Requester)) {
		return FsAccessDenied;
	}

	/* Case 3 - End of File */
	if (fHandle->Position == fHandle->File->Size) {
		*BytesRead = 0;
		return FsOk;
	}

	/* Sanity -> Flush if we wrote and now read */
	if (fHandle->LastOperation != __FILE_OPERATION_READ) {
		Code = VfsFlush(Handle);
	}

	/* Instantiate the pointer and read */
	Fs = (FileSystem_t*)fHandle->File->System;
	Code = Fs->Module->ReadFile(&Fs->Descriptor, fHandle, BufferObject, BytesRead);

	/* Update stats for the handle */
	fHandle->LastOperation = __FILE_OPERATION_READ;
	fHandle->Position += *BytesRead;
	return Code;
}

/* WriteFile
 * Writes the requested number of bytes from the given buffer
 * into the current position in the filehandle */
FileSystemCode_t
WriteFile(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_In_ BufferObject_t *BufferObject,
	_Out_ size_t *BytesWritten)
{
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	FileSystemCode_t Code = FsOk;
	ListNode_t *hNode = NULL;
	FileSystem_t *Fs = NULL;
	int WriteToDisk = 0;
	DataKey_t Key;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = ListGetNodeByKey(VfsGetOpenHandles(), Key, 0);

	/* Case 1 - Not found / Invalid parameters */
	if (hNode == NULL
		|| BufferObject == NULL
		|| BufferObject->Length == 0) {
		return FsInvalidParameters;
	}

	/* Instantiate pointer for next check */
	fHandle = (FileSystemFileHandle_t*)hNode->Data;

	/* Case 2 - Invalid Owner / Missing Access */
	if (fHandle->Owner != Requester
		|| !(fHandle->Access & __FILE_WRITE_ACCESS)
		|| (fHandle->File->IsLocked != UUID_INVALID
			&& fHandle->File->IsLocked != Requester)) {
		return FsAccessDenied;
	}

	/* Sanity -> Clear read buffer if we are writing */
	if (fHandle->LastOperation != __FILE_OPERATION_WRITE) {
		Code = VfsFlush(Handle);
	}

	/* Instantiate the pointer */
	Fs = (FileSystem_t*)fHandle->File->System;

	/* Write to buffer if we can 
	 * - This needs severely improved support for
	 * bufferobjects before this works */
#if 0
	if (!(fHandle->Options & __FILE_VOLATILE)) {
		size_t BytesAvailable = Fs->Descriptor.Disk.Descriptor.SectorSize
			- fHandle->OutBufferPosition;

		/* Do we have enough room for the entire transaction? */
		if (BufferObject->Length < BytesAvailable) {
			uint8_t *bPtr = (uint8_t*)fHandle->OutBuffer;
			memcpy((bPtr + fHandle->OutBufferPosition), 
				BufferObject->Virtual, BufferObject->Length);
			fHandle->OutBufferPosition += BufferObject->Length;
			*BytesWritten = BufferObject->Length;
		}
		else { /* This is only really neccesary if we actually used the buffer */
			if (fHandle->OutBufferPosition != 0)
			{
				/* Allocate a temporary buffer big enough */
				uint8_t *TemporaryBuffer =
					(uint8_t*)malloc(fHandle->OutBufferPosition + BufferObject->Length);

				/* Copy data over */
				memcpy(TemporaryBuffer, Handle->oBuffer, Handle->oBufferPosition);
				memcpy(TemporaryBuffer + Handle->oBufferPosition, Buffer, Length);

				/* Write to Disk */
				BytesWritten = Fs->WriteFile(Fs, Handle, TempBuffer, 
					Handle->oBufferPosition + Length);

				/* Sanity */
				if (BytesWritten > Length)
					BytesWritten = Length;

				/* Free temporary buffer */
				free(TempBuffer);

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
#else
	WriteToDisk = 1;
#endif

	/* Deep Write */
	if (WriteToDisk) {
		Code = Fs->Module->WriteFile(&Fs->Descriptor, 
			fHandle, BufferObject, BytesWritten);
	}

	/* Update stats for the handle */
	fHandle->LastOperation = __FILE_OPERATION_WRITE;
	fHandle->Position += *BytesWritten;
	return Code;
}

/* SeekFile
 * Sets the file-pointer for the given handle to the
 * values given, the position is absolute and must
 * be within range of the file size */
FileSystemCode_t
SeekFile(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle, 
	_In_ uint32_t SeekLo, 
	_In_ uint32_t SeekHi)
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

/* Vfs - Flush Handle
* @Handle - A valid file handle */
FileSystemCode_t VfsFlush(UUId_t Handle)
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
