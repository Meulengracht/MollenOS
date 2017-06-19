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
 * MollenOS MCore - Contract Definitions & Structures (FileSystem Contract)
 * - This header describes the filesystem contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_FILESYSTEM_INTERFACE_H_
#define _CONTRACT_FILESYSTEM_INTERFACE_H_

/* Includes 
 * - System */
#include <os/driver/contracts/storage.h>
#include <os/driver/buffer.h>
#include <os/driver/driver.h>
#include <os/osdefs.h>

/* Includes
 * - File System */
#include <os/driver/file/definitions.h>
#include <os/driver/file/file.h>
#include <os/driver/file/path.h>

/* FileSystem Export 
 * This is define the interface between user (filemanager)
 * and the implementer (the filesystem) */
#ifdef __FILEMANAGER_IMPL
#define __FSAPI					typedef
#define __FSDECL(Function)		(*Function##_t)
#else
#define __FSAPI					__declspec(dllexport)
#define __FSDECL(Function)		Function
#endif

/* FileSystem definitions 
 * Used the describe the various possible flags for
 * the given filesystem */
#define __FILESYSTEM_BOOT				0x00000001

/* FileSystem Disk structure
 * Keeps information about the disk target and the
 * general information about the disk (geometry, string data) */
PACKED_TYPESTRUCT(FileSystemDisk, {
	UUId_t					Driver;
	UUId_t					Device;
	Flags_t					Flags;
	StorageDescriptor_t		Descriptor;
});

/* The filesystem descriptor structure 
 * Contains basic information about the filesystem
 * and holds a copy of the disk information to provide
 * disk access and information */
PACKED_TYPESTRUCT(FileSystemDescriptor, {
	Flags_t					 Flags;
	FileSystemDisk_t		 Disk;
	uint64_t				 SectorStart;
	uint64_t				 SectorCount;
	uintptr_t				*ExtensionData;
});

/* FsInitialize 
 * Initializes a new instance of the file system
 * and allocates resources for the given descriptor */
__FSAPI
OsStatus_t
__FSDECL(FsInitialize)(
	_InOut_ FileSystemDescriptor_t *Descriptor);

/* FsDestroy 
 * Destroys the given filesystem descriptor and cleans
 * up any resources allocated by the filesystem instance */
__FSAPI
OsStatus_t
__FSDECL(FsDestroy)(
	_InOut_ FileSystemDescriptor_t *Descriptor,
	_In_ Flags_t UnmountFlags);

/* FsOpenFile 
 * Opens a new link to a file and allocates resources
 * for a new open-file in the system */
__FSAPI
FileSystemCode_t 
__FSDECL(FsOpenFile)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_Out_ FileSystemFile_t *File,
	_In_ MString_t *Path);

/* FsCreateFile 
 * Creates a new link to a file and allocates resources
 * for a new open-file in the system */
__FSAPI
FileSystemCode_t 
__FSDECL(FsCreateFile)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_Out_ FileSystemFile_t *File,
	_In_ MString_t *Path,
	_In_ Flags_t Options);

/* FsCloseFile 
 * Closes the given file-link and frees all resources
 * this is only invoked once all handles has been closed
 * to that file link, or the file-system is unmounted */
__FSAPI
FileSystemCode_t
__FSDECL(FsCloseFile)(
	_In_ FileSystemDescriptor_t *Descriptor, 
	_In_ FileSystemFile_t *File);

/* FsChangeFileSize 
 * Either expands or shrinks the allocated space for the given
 * file-handle to the requested size. */
__FSAPI
FileSystemCode_t
__FSDECL(FsChangeFileSize)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFile_t *Handle,
	_In_ uint64_t Size);

/* FsOpenHandle 
 * Opens a new handle to a file, this allows various
 * interactions with the base file, like read and write.
 * Neccessary resources and initialization of the Handle
 * should be done here too */
__FSAPI
FileSystemCode_t
__FSDECL(FsOpenHandle)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle);

/* FsCloseHandle 
 * Closes the file handle and cleans up any resources allocated
 * by the OpenHandle equivelent. Renders the handle useless */
__FSAPI
FileSystemCode_t
__FSDECL(FsCloseHandle)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle);

/* FsReadFile 
 * Reads the requested number of bytes from the given
 * file handle and outputs the number of bytes actually read */
__FSAPI
FileSystemCode_t
__FSDECL(FsReadFile)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle,
	_Out_ BufferObject_t *BufferObject,
	_Out_ size_t *BytesAt,
	_Out_ size_t *BytesRead);

/* FsWriteFile 
 * Writes the requested number of bytes to the given
 * file handle and outputs the number of bytes actually written */
__FSAPI
FileSystemCode_t
__FSDECL(FsWriteFile)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle,
	_In_ BufferObject_t *BufferObject,
	_Out_ size_t *BytesWritten);

/* FsSeekFile 
 * Seeks in the given file-handle to the absolute position
 * given, must be within boundaries otherwise a seek won't
 * take a place */
__FSAPI
FileSystemCode_t
__FSDECL(FsSeekFile)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle,
	_In_ uint64_t AbsolutePosition);

/* FsDeleteFile 
 * Deletes the file connected to the file-handle, this
 * will disconnect all existing file-handles to the file
 * and make them fail on next access */
__FSAPI
FileSystemCode_t
__FSDECL(FsDeleteFile)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle);

/* FsQueryFile 
 * Queries the given file handle for information, the kind of
 * information queried is determined by the function */
__FSAPI
FileSystemCode_t
__FSDECL(FsQueryFile)(
	_In_ FileSystemDescriptor_t *Descriptor,
	_In_ FileSystemFileHandle_t *Handle,
	_In_ int Function,
	_Out_ void *Buffer,
	_In_ size_t MaxLength);

#endif //!_CONTRACT_FILESYSTEM_INTERFACE_H_
