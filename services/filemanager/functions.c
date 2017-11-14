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
 * - ToDo Buffering is not ported to BufferObjects yet
 */

/* Includes 
 * - System */
#include <os/driver/file.h>
#include "include/vfs.h"
#include <os/mollenos.h>

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
	Handle->LastOperation = __FILE_OPERATION_NONE;
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
	// Variables
	CollectionItem_t *fNode = NULL, *pNode = NULL;
	FileSystemFile_t *File = NULL;
	FileSystemCode_t Code = FsOk;
	MString_t *Identifier = NULL;
	MString_t *SubPath = NULL;
	size_t PathHash = 0;
	DataKey_t Key;
	int Index = 0;

	// To ensure that we don't spend resources doing
	// the wheel all over again, compute the hash 
	// and check cache 
	PathHash = MStringHash(Path);
	Key.Value = (int)PathHash;
	pNode = CollectionGetNodeByKey(VfsGetOpenFiles(), Key, 0);
	
	if (pNode != NULL) {
		FileSystemFile_t *NodeFile = (FileSystemFile_t*)pNode->Data;

		// If file is locked, bad luck 
		// - Otherwise open a handle and increase ref-count
		if (NodeFile->IsLocked != UUID_INVALID) {
			return FsAccessDenied;
		}
		else {
			// It's important here that we check if the flag
			// __FILE_FAILONEXIST has been set, then we return
			// the appropriate code instead of opening a new handle
			if (Handle->Options & __FILE_FAILONEXIST) {
				return FsPathExists;
			}

			// Store it
			File = NodeFile;
		}
	}

	// Ok if it didn't exist in cache it's a new lookup
	if (File != NULL) {
		// To open a new file we need to find the correct
		// filesystem identifier and seperate it from it's absolute path
		Index = MStringFind(Path, ':');
		Identifier = MStringSubString(Path, 0, Index);
		SubPath = MStringSubString(Path, Index + 2, -1);

		// Iterate all the filesystems and find the one
		// that matches
		_foreach(fNode, VfsGetFileSystems()) {
			FileSystem_t *Fs = (FileSystem_t*)fNode->Data;
			if (MStringCompare(Identifier, Fs->Identifier, 1)) {
				// Variables
				int Created = 0;

				// We found it, allocate a new file structure and prefill
				// some information, the module call will fill rest
				File = (FileSystemFile_t*)malloc(sizeof(FileSystemFile_t));
				memset(File, 0, sizeof(FileSystemFile_t));
				File->System = (uintptr_t*)Fs;
				File->Path = MStringCreate((void*)MStringRaw(Path), StrUTF8);
				File->Hash = PathHash;

				// Let the module do the rest
				Code = Fs->Module->OpenFile(&Fs->Descriptor, File, SubPath);

				// Handle the creation flag
				if (Code == FsPathNotFound && (Handle->Options & __FILE_CREATE)) {
					Code = Fs->Module->CreateFile(&Fs->Descriptor, File, SubPath, 0);
					Created = 1;
				}

				// Sanitize the open
				// otherwise we must cleanup
				if (Code == FsOk) {
					// It's important here that we check if the flag
					// __FILE_FAILONEXIST has been set, then we return
					// the appropriate code instead of opening a new handle
					// Also this is ok if file was just created
					if (Handle->Options & __FILE_FAILONEXIST) {
						if (Created == 0) {
							Code = Fs->Module->CloseFile(&Fs->Descriptor, File);
							MStringDestroy(File->Path);
							free(File);
							File = NULL;
							break;
						}
					}

					// Take care of truncation flag
					if (Handle->Options & __FILE_TRUNCATE) {
						Code = Fs->Module->ChangeFileSize(&Fs->Descriptor, File, 0);
					}

					// Append file handle
					CollectionAppend(VfsGetOpenFiles(), CollectionCreateNode(Key, File));
					File->References = 1;
				}
				else {
					MStringDestroy(File->Path);
					free(File);
					File = NULL;
				}

				// Done - break out
				break;
			}
		}

		// Cleanup
		MStringDestroy(Identifier);
		MStringDestroy(SubPath);
	}

	// Now we can open the handle
	// Open Handle Internal takes care of these flags
	// APPEND/VOLATILE/BINARY
	if (File != NULL) {
		Code = VfsOpenHandleInternal(Handle, File);
		if (Code == FsOk) {
			File->References++;
		}
	}
	
	// Return the result
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
				Code = FsPathNotFound;
				continue;
			}
			else {
				Code = VfsOpenInternal(hFile, mPath);
			}
			
			/* Cleanup path before continuing */
			MStringDestroy(mPath);

			/* Sanitize the status in which we tried to
			 * open the current path */
			if ((Code == FsPathNotFound
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
			Code = FsPathNotFound;
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
		CollectionAppend(VfsGetOpenHandles(),
			CollectionCreateNode(Key, hFile));
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
	CollectionItem_t *hNode = NULL;
	FileSystem_t *Fs = NULL;
	DataKey_t Key;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);

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
		FlushFile(Requester, Handle);
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
		fHandle->File->IsLocked = UUID_INVALID;
	}

	/* Last reference?
	 * Cleanup the file in case of no refs */
	if (fHandle->File->References <= 0) {
		CollectionItem_t *pNode = CollectionGetNodeByKey(VfsGetOpenFiles(), Key, 0);

		/* Cleanup the file */
		Code = Fs->Module->CloseFile(&Fs->Descriptor, fHandle->File);
		MStringDestroy(fHandle->File->Path);
		free(fHandle->File);

		/* Remove from list */
		if (pNode != NULL) {
			CollectionRemoveByNode(VfsGetOpenFiles(), pNode);
			free(pNode);
		}
	}

	/* Remove the handle from list */
	CollectionRemoveByNode(VfsGetOpenHandles(), hNode);
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
	CollectionItem_t *hNode = NULL;
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
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);
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
	_Out_ size_t *BytesIndex,
	_Out_ size_t *BytesRead)
{
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	FileSystemCode_t Code = FsOk;
	CollectionItem_t *hNode = NULL;
	FileSystem_t *Fs = NULL;
	DataKey_t Key;

	/* Sanitize request parameters first
	* Is handle valid? */
	Key.Value = (int)Handle;
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);

	/* Case 1 - Not found / Invalid parameters */
	if (hNode == NULL
		|| BufferObject == NULL
		|| GetBufferSize(BufferObject) == 0) {
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
		Code = FlushFile(Requester, Handle);
	}

	/* Instantiate the pointer and read */
	Fs = (FileSystem_t*)fHandle->File->System;
	Code = Fs->Module->ReadFile(&Fs->Descriptor, fHandle, 
		BufferObject, BytesIndex, BytesRead);

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
	CollectionItem_t *hNode = NULL;
	FileSystem_t *Fs = NULL;
	int WriteToDisk = 0;
	DataKey_t Key;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);

	/* Case 1 - Not found / Invalid parameters */
	if (hNode == NULL
		|| BufferObject == NULL
		|| GetBufferSize(BufferObject) == 0) {
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
		Code = FlushFile(Requester, Handle);
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
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	FileSystemCode_t Code = FsOk;
	CollectionItem_t *hNode = NULL;
	FileSystem_t *Fs = NULL;
	DataKey_t Key;

	/* Combine two u32 to form one big u64 
	 * This is just the declaration */
	union {
		struct {
			uint32_t Lo;
			uint32_t Hi;
		} Parts;
		uint64_t Full;
	} SeekAbs;

	/* Fill in values */
	SeekAbs.Parts.Lo = SeekLo;
	SeekAbs.Parts.Hi = SeekHi;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);

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

	/* Flush buffers before seeking */
	if (!(fHandle->Options & __FILE_VOLATILE)) {
		Code = FlushFile(Requester, Handle);
	}

	/* Instantiate the filesystem pointer */
	Fs = (FileSystem_t*)fHandle->File->System;

	/* Seek into file */
	Code = Fs->Module->SeekFile(&Fs->Descriptor, fHandle, SeekAbs.Full);

	/* Clear a few variables - needs to be
	 * done at each seek */
	fHandle->LastOperation = __FILE_OPERATION_NONE;
	fHandle->OutBufferPosition = 0;
	return Code;
}

/* FlushFile
 * Flushes the internal file buffers and ensures there are
 * no pending file operations for the given file handle */
FileSystemCode_t
FlushFile(
	_In_ UUId_t Requester, 
	_In_ UUId_t Handle)
{
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	FileSystemCode_t Code = FsOk;
	CollectionItem_t *hNode = NULL;
	FileSystem_t *Fs = NULL;
	DataKey_t Key;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);

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

	/* Case 3 - No buffering enabled */
	if (fHandle->Options & __FILE_VOLATILE) {
		return FsOk;
	}

	/* Instantiate the filesystem pointer */
	Fs = (FileSystem_t*)fHandle->File->System;

	/* Empty output buffer 
	 * - But sanitize the buffers first */
	if (fHandle->OutBuffer != NULL
		&& fHandle->OutBufferPosition != 0) {
		size_t BytesWritten = 0;
#if 0
		Code = Fs->Module->WriteFile(&Fs->Descriptor, fHandle, NULL, &BytesWritten);
#endif
		if (BytesWritten != fHandle->OutBufferPosition) {
			return FsDiskError;
		}
	}

	return Code;
}

/* MoveFile
 * Moves or copies a given file path to the destination path
 * this can also be used for renamining if the dest/source paths
 * match (except for filename/directoryname) */
FileSystemCode_t
MoveFile(
	_In_ UUId_t Requester,
	_In_ __CONST char *Source, 
	_In_ __CONST char *Destination,
	_In_ int Copy)
{
	_CRT_UNUSED(Requester);
	_CRT_UNUSED(Source);
	_CRT_UNUSED(Destination);
	_CRT_UNUSED(Copy);
	return FsOk;
}

/* GetFilePosition 
 * Queries the current file position that the given handle
 * is at, it returns as two seperate unsigned values, the upper
 * value is optional and should only be checked for large files */
OsStatus_t
GetFilePosition(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_Out_ QueryFileValuePackage_t *Result)
{
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	CollectionItem_t *hNode = NULL;
	DataKey_t Key;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);

	/* Case 1 - Not found */
	if (hNode == NULL) {
		Result->Code = FsInvalidParameters;
		return OsError;
	}

	/* Instantiate pointer for next check */
	fHandle = (FileSystemFileHandle_t*)hNode->Data;

	/* Case 2 - Invalid Owner */
	if (fHandle->Owner != Requester) {
		Result->Code = FsAccessDenied;
		return OsError;
	}

	/* Fill in information */
	Result->Value.Full = fHandle->Position;
	Result->Code = FsOk;
	return OsSuccess;
}

/* GetFileOptions
 * Queries the current file options and file access flags
 * for the given file handle */
OsStatus_t
GetFileOptions(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_Out_ QueryFileOptionsPackage_t *Result)
{
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	CollectionItem_t *hNode = NULL;
	DataKey_t Key;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);

	/* Case 1 - Not found */
	if (hNode == NULL) {
		Result->Code = FsInvalidParameters;
		return OsError;
	}

	/* Instantiate pointer for next check */
	fHandle = (FileSystemFileHandle_t*)hNode->Data;

	/* Case 2 - Invalid Owner */
	if (fHandle->Owner != Requester) {
		Result->Code = FsAccessDenied;
		return OsError;
	}

	/* Fill in information */
	Result->Options = fHandle->Options;
	Result->Access = fHandle->Access;
	Result->Code = FsOk;
	return OsSuccess;
}

/* SetFileOptions 
 * Attempts to modify the current option and or access flags
 * for the given file handle as specified by <Options> and <Access> */
OsStatus_t
SetFileOptions(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_In_ Flags_t Options,
	_In_ Flags_t Access)
{
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	CollectionItem_t *hNode = NULL;
	DataKey_t Key;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);

	/* Case 1 - Not found */
	if (hNode == NULL) {
		return OsError;
	}

	/* Instantiate pointer for next check */
	fHandle = (FileSystemFileHandle_t*)hNode->Data;

	/* Case 2 - Invalid Owner */
	if (fHandle->Owner != Requester) {
		return OsError;
	}

	/* Update the requested information */
	fHandle->Options = Options;
	fHandle->Access = Access;
	return OsSuccess;
}

/* GetFileSize 
 * Queries the current file size that the given handle
 * has, it returns as two seperate unsigned values, the upper
 * value is optional and should only be checked for large files */
OsStatus_t
GetFileSize(
	_In_ UUId_t Requester,
	_In_ UUId_t Handle,
	_Out_ QueryFileValuePackage_t *Result)
{
	/* Variables */
	FileSystemFileHandle_t *fHandle = NULL;
	CollectionItem_t *hNode = NULL;
	DataKey_t Key;

	/* Sanitize request parameters first
	 * Is handle valid? */
	Key.Value = (int)Handle;
	hNode = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);

	/* Case 1 - Not found */
	if (hNode == NULL) {
		Result->Code = FsInvalidParameters;
		return OsError;
	}

	/* Instantiate pointer for next check */
	fHandle = (FileSystemFileHandle_t*)hNode->Data;

	/* Case 2 - Invalid Owner */
	if (fHandle->Owner != Requester) {
		Result->Code = FsAccessDenied;
		return OsError;
	}

	/* Fill in information */
	Result->Value.Full = fHandle->File->Size;
	Result->Code = FsOk;
	return OsSuccess;
}
