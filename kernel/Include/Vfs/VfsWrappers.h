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
* - VFS Wrappers for internal usage, to make code more clear
*/

#ifndef _MCORE_VFS_WRAPPERS_H_
#define _MCORE_VFS_WRAPPERS_H_

/* Includes 
 * - System */
#include <DeviceManager.h>
#include <Vfs/Vfs.h>
#include <Heap.h>

/* Vfs - Flush Handle
 * @Handle - A valid file handle */
static VfsErrorCode_t VfsWrapperFlush(MCoreFileInstance_t *Handle)
{
	/* Create a new request with the VFS
	 * Ask it to flush the file */
	MCoreVfsRequest_t *Request =
		(MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));
	VfsErrorCode_t RetCode = VfsOk;

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestFlushFile;

	/* Setup params for the request */
	Request->Pointer.Handle = Handle;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store return code */
	RetCode = Request->Error;

	/* Cleanup */
	kfree(Request);

	/* Done! */
	return RetCode;
}

/* Vfs - Open File
 * @Path - UTF-8 String
 * @OpenFlags - Kind of Access */
static MCoreFileInstance_t *VfsWrapperOpen(const char *Path, VfsFileFlags_t OpenFlags)
{
	/* Create a new request with the VFS
	 * Ask it to open the file */
	MCoreFileInstance_t *Instance = NULL;
	MCoreVfsRequest_t *Request =
		(MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestOpenFile;

	/* Setup params for the request */
	Request->Pointer.Path = Path;
	Request->Value.Lo.Flags = OpenFlags;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store handle */
	Instance = Request->Pointer.Handle;

	/* Cleanup */
	kfree(Request);

	/* Done! */
	return Instance;
}

/* Vfs - Close File
 * @Handle - A valid file handle */
static VfsErrorCode_t VfsWrapperClose(MCoreFileInstance_t *Handle)
{
	/* Create a new request with the VFS
	 * Ask it to close the file */
	MCoreVfsRequest_t *Request =
		(MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));
	VfsErrorCode_t RetCode = VfsOk;

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestCloseFile;

	/* Setup params for the request */
	Request->Pointer.Handle = Handle;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store return code */
	RetCode = Request->Error;

	/* Cleanup */
	kfree(Request);

	/* Done! */
	return RetCode;
}

/* Vfs - Read File
 * @Handle - A valid file handle
 * @Buffer - A valid data buffer
 * @Length - How many bytes of data to read */
static size_t VfsWrapperRead(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length)
{
	/* Variables */
	MCoreVfsRequest_t *Request = NULL;
	size_t bRead = 0;

	/* Create a new request with the VFS
	* Ask it to read the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestReadFile;

	/* Setup params for the request */
	Request->Pointer.Handle = Handle;
	Request->Buffer = Buffer;
	Request->Value.Lo.Length = Length;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store bytes read */
	bRead = Request->Value.Hi.Length;

	/* Cleanup */
	kfree(Request);

	/* Done! */
	return bRead;
}

/* Vfs - Write File
 * @Handle - A valid file handle
 * @Buffer - A valid data buffer
 * @Length - How many bytes of data to write */
static size_t VfsWrapperWrite(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length)
{
	/* Variables */
	MCoreVfsRequest_t *Request = NULL;
	size_t bWritten = 0;

	/* Create a new request with the VFS
	* Ask it to read the file */
	Request = (MCoreVfsRequest_t*)kmalloc(sizeof(MCoreVfsRequest_t));

	/* Reset request */
	memset(Request, 0, sizeof(MCoreVfsRequest_t));

	/* Setup base request */
	Request->Base.Type = VfsRequestWriteFile;

	/* Setup params for the request */
	Request->Pointer.Handle = Handle;
	Request->Buffer = Buffer;
	Request->Value.Lo.Length = Length;

	/* Send the request */
	VfsRequestCreate(Request);
	VfsRequestWait(Request, 0);

	/* Store bytes read */
	bWritten = Request->Value.Hi.Length;

	/* Cleanup */
	kfree(Request);

	/* Done! */
	return bWritten;
}

#endif //!_MCORE_VFS_WRAPPERS_H_
