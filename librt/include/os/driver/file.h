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
 * MollenOS MCore - File Definitions & Structures
 * - This header describes the base file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _FILE_INTERFACE_H_
#define _FILE_INTERFACE_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>
#include <os/ipc/ipc.h>
#include <os/driver/server.h>

/* The export macro, and is only set by the
 * the actual implementation of the filemanager */
#ifdef __FILEMANAGER_IMPL
#define __FILEAPI __EXTERN
#else
#define __FILEAPI static __CRT_INLINE
#endif

/* These definitions are in-place to allow a custom
 * setting of the file-manager, these are set to values
 * where in theory it should never be needed to have more */
#define __FILEMANAGER_INTERFACE_VERSION		1

/* These are the different IPC functions supported
 * by the filemanager, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __FILEMANAGER_REGISTERDISK				IPC_DECL_FUNCTION(0)
#define __FILEMANAGER_UNREGISTERDISK			IPC_DECL_FUNCTION(1)

#define __FILEMANAGER_OPENFILE					IPC_DECL_FUNCTION(2)
#define __FILEMANAGER_CLOSEFILE					IPC_DECL_FUNCTION(3)
#define __FILEMANAGER_DELETEFILE				IPC_DECL_FUNCTION(4)
#define __FILEMANAGER_READFILE					IPC_DECL_FUNCTION(5)
#define __FILEMANAGER_WRITEFILE					IPC_DECL_FUNCTION(6)
#define __FILEMANAGER_SEEKFILE					IPC_DECL_FUNCTION(7)
#define __FILEMANAGER_FLUSHFILE					IPC_DECL_FUNCTION(8)
#define __FILEMANAGER_MOVEFILE					IPC_DECL_FUNCTION(9)

/* Bit flag defintions for operations such as 
 * registering / unregistering of disks, open flags
 * for OpenFile and access flags */
#define __DISK_REMOVABLE						0x00000001

#define __DISK_FORCED_REMOVE					0x00000001

#define __FILE_READ_ACCESS						0x00000001
#define __FILE_WRITE_ACCESS						0x00000002
#define __FILE_READ_SHARE						0x00000100
#define __FILE_WRITE_SHARE						0x00000200

#define __FILE_CREATE							0x00000001
#define __FILE_TRUNCATE							0x00000002
#define __FILE_MUSTEXIST						0x00000004
#define __FILE_APPEND							0x00000008

/* RegisterDisk
 * Registers a disk with the file-manager and it will
 * automatically be parsed (MBR, GPT, etc), and all filesystems
 * on the disk will be brought online */
#ifdef __FILEMANAGER_IMPL
__FILEAPI OsStatus_t RegisterDisk(UUId_t Driver, UUId_t Device, Flags_t Flags);
#else
__FILEAPI OsStatus_t RegisterDisk(UUId_t Driver, UUId_t Device, Flags_t Flags)
{

}
#endif

/* UnregisterDisk
 * Unregisters a disk from the system, and brings any filesystems
 * registered on this disk offline */
#ifdef __FILEMANAGER_IMPL
__FILEAPI OsStatus_t UnregisterDisk(UUId_t Device, Flags_t Flags);
#else
__FILEAPI OsStatus_t UnregisterDisk(UUId_t Device, Flags_t Flags)
{

}
#endif

/* OpenFile
 * */
#ifdef __FILEMANAGER_IMPL
__FILEAPI UUId_t OpenFile(UUId_t Requester, 
	const char *Path, Flags_t Options, Flags_t Access);
#else
__FILEAPI UUId_t OpenFile(const char *Path, Flags_t Options, Flags_t Access)
{

}
#endif

/* CloseFile
 * */
#ifdef __FILEMANAGER_IMPL
__FILEAPI OsStatus_t CloseFile(UUId_t Requester, UUId_t Handle);
#else
__FILEAPI OsStatus_t CloseFile(UUId_t Handle)
{

}
#endif

/* DeleteFile
 * */
#ifdef __FILEMANAGER_IMPL
__FILEAPI OsStatus_t DeleteFile(UUId_t Requester, const char *Path);
#else
__FILEAPI OsStatus_t DeleteFile(const char *Path)
{

}
#endif

/* ReadFile
 * */

/* WriteFile
 * */

/* SeekFile
 * */
#ifdef __FILEMANAGER_IMPL
__FILEAPI OsStatus_t SeekFile(UUId_t Requester, 
	UUId_t Handle, uint32_t SeekLo, uint32_t SeekHi);
#else
__FILEAPI OsStatus_t SeekFile(UUId_t Handle, uint32_t SeekLo, uint32_t SeekHi)
{

}
#endif

/* FlushFile
 * */
#ifdef __FILEMANAGER_IMPL
__FILEAPI OsStatus_t FlushFile(UUId_t Requester, UUId_t Handle);
#else
__FILEAPI OsStatus_t FlushFile(UUId_t Handle)
{

}
#endif

/* MoveFile
 * */
#ifdef __FILEMANAGER_IMPL
__FILEAPI OsStatus_t MoveFile(UUId_t Requester, 
	const char *Source, const char *Destination, int Copy);
#else
__FILEAPI OsStatus_t MoveFile(const char *Source, const char *Destination, int Copy)
{

}
#endif

#endif //!_DEVICE_INTERFACE_H_
