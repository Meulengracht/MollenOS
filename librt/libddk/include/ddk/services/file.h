/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * File Service (Protected) Definitions & Structures
 * - This header describes the base file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_SERVICES_FILE_H__
#define __DDK_SERVICES_FILE_H__

#include <ddk/ddkdefs.h>
#include <ddk/ipc/ipc.h>
#include <os/types/file.h>

PACKED_TYPESTRUCT(OpenFilePackage, {
    FileSystemCode_t    Code;
    UUId_t              Handle;
});
PACKED_TYPESTRUCT(RWFilePackage, {
    FileSystemCode_t    Code;
    size_t              Index;
    size_t              ActualSize;
});
PACKED_TYPESTRUCT(QueryFileValuePackage, {
    FileSystemCode_t    Code;
    union {
        struct {
            uint32_t    Lo;
            uint32_t    Hi;
        } Parts;
        uint64_t        Full;
    } Value;
});
PACKED_TYPESTRUCT(QueryFileOptionsPackage, {
    FileSystemCode_t    Code;
    Flags_t             Options;
    Flags_t             Access;
});
PACKED_TYPESTRUCT(QueryFileStatsPackage, {
    FileSystemCode_t    Code;
    OsFileDescriptor_t  Descriptor;
});

PACKED_TYPESTRUCT(FileMappingParameters, {
    UUId_t    MemoryHandle;
    Flags_t   Flags;
    uint64_t  FileOffset;
    uintptr_t VirtualAddress;
    size_t    Length;
});

/* These definitions are in-place to allow a custom
 * setting of the file-manager, these are set to values
 * where in theory it should never be needed to have more */
#define __FILEMANAGER_INTERFACE_VERSION        1

/* These are the different IPC functions supported
 * by the filemanager, note that some of them might
 * be changed in the different versions, and/or new functions will be added */
#define __FILEMANAGER_REGISTERDISK              IPC_DECL_FUNCTION(0)
#define __FILEMANAGER_UNREGISTERDISK            IPC_DECL_FUNCTION(1)
#define __FILEMANAGER_QUERYDISKS                IPC_DECL_FUNCTION(2)
#define __FILEMANAGER_QUERYDISK                 IPC_DECL_FUNCTION(3)
#define __FILEMANAGER_QUERYDISKBYPATH           IPC_DECL_FUNCTION(4)
#define __FILEMANAGER_QUERYDISKBYHANDLE         IPC_DECL_FUNCTION(5)

#define __FILEMANAGER_OPEN                      IPC_DECL_FUNCTION(6)
#define __FILEMANAGER_CLOSE                     IPC_DECL_FUNCTION(7)
#define __FILEMANAGER_READ                      IPC_DECL_FUNCTION(8)
#define __FILEMANAGER_WRITE                     IPC_DECL_FUNCTION(9)
#define __FILEMANAGER_SEEK                      IPC_DECL_FUNCTION(10)
#define __FILEMANAGER_FLUSH                     IPC_DECL_FUNCTION(11)
#define __FILEMANAGER_MOVE                      IPC_DECL_FUNCTION(12)

#define __FILEMANAGER_GETPOSITION               IPC_DECL_FUNCTION(13)
#define __FILEMANAGER_GETOPTIONS                IPC_DECL_FUNCTION(14)
#define __FILEMANAGER_SETOPTIONS                IPC_DECL_FUNCTION(15)
#define __FILEMANAGER_GETSIZE                   IPC_DECL_FUNCTION(16)
#define __FILEMANAGER_GETPATH                   IPC_DECL_FUNCTION(17)
#define __FILEMANAGER_GETSTATSBYPATH            IPC_DECL_FUNCTION(18)
#define __FILEMANAGER_GETSTATSBYHANDLE          IPC_DECL_FUNCTION(19)
#define __FILEMANAGER_DELETEPATH                IPC_DECL_FUNCTION(20)

#define __FILEMANAGER_PATHRESOLVE               IPC_DECL_FUNCTION(21)
#define __FILEMANAGER_PATHCANONICALIZE          IPC_DECL_FUNCTION(22)

#define __FILEMANAGER_REGISTERMAPPING           IPC_DECL_FUNCTION(23)
#define __FILEMANAGER_UNREGISTERMAPPING         IPC_DECL_FUNCTION(24)

// RegisterStorage::Flags
#define __STORAGE_REMOVABLE     0x00000001

// UnregisterStorage::Flags
#define __STORAGE_FORCED_REMOVE 0x00000001

/* RegisterStorage
 * Registers a disk with the file-manager and it will
 * automatically be parsed (MBR, GPT, etc), and all filesystems
 * on the disk will be brought online */
DDKDECL(OsStatus_t,
RegisterStorage( 
    _In_ UUId_t  Device, 
    _In_ Flags_t Flags));
    
/* UnregisterStorage
 * Unregisters a disk from the system, and brings any filesystems
 * registered on this disk offline */
DDKDECL(OsStatus_t,
UnregisterStorage(
    _In_ UUId_t  Device,
    _In_ Flags_t Flags));

#endif //!__DDK_SERVICES_FILE_H__
