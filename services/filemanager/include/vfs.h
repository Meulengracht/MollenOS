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
 * Virtual File Definitions & Structures
 * - This header describes the base virtual file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _VFS_INTERFACE_H_
#define _VFS_INTERFACE_H_

#include <ddk/filesystem.h>
#include <os/types/path.h>
#include <ds/collection.h>
#include <os/mollenos.h>
#include <ds/mstring.h>

/* VFS Definitions 
 * - General identifiers can be used in paths */
#define __FILEMANAGER_MAXDISKS          64

#define __FILE_OPERATION_NONE           0x00000000
#define __FILE_OPERATION_READ           0x00000001
#define __FILE_OPERATION_WRITE          0x00000002

typedef enum _FileSystemType {
    FSUnknown = 0,
    FSFAT,
    FSEXFAT,
    FSNTFS,
    FSHFS,
    FSHPFS,
    FSMFS,
    FSEXT
} FileSystemType_t;

/* VFS FileSystem Module 
 * Contains all the protocols implemented
 * by each filesystem module, also contains
 * the number of references the individual module */
typedef struct _FileSystemModule {
    FileSystemType_t    Type;
    int                 References;
    Handle_t            Handle;

    FsInitialize_t      Initialize;
    FsDestroy_t         Destroy;
    FsOpenEntry_t       OpenEntry;
    FsCreatePath_t      CreatePath;
    FsCloseEntry_t      CloseEntry;
    FsDeleteEntry_t     DeleteEntry;
    FsChangeFileSize_t  ChangeFileSize;
    FsOpenHandle_t      OpenHandle;
    FsCloseHandle_t     CloseHandle;
    FsReadEntry_t       ReadEntry;
    FsWriteEntry_t      WriteEntry;
    FsSeekInEntry_t     SeekInEntry;
} FileSystemModule_t;

/* VFS FileSystem structure
 * this is the main file-system structure 
 * and contains everything related to a filesystem 
 * represented in MCore */
typedef struct _FileSystem {
    UUId_t                      Id;
    FileSystemType_t            Type;
    MString_t*                  Identifier;
    FileSystemDescriptor_t      Descriptor;
    FileSystemModule_t*         Module;
} FileSystem_t;

/* DiskRegisterFileSystem 
 * Registers a new filesystem of the given type, on
 * the given disk with the given position on the disk 
 * and assigns it an identifier */
__EXTERN OsStatus_t
DiskRegisterFileSystem(
    _In_ FileSystemDisk_t*  Disk,
    _In_ uint64_t           Sector,
    _In_ uint64_t           SectorCount,
    _In_ FileSystemType_t   Type);

/* DiskDetectFileSystem
 * Detectes the kind of filesystem at the given absolute sector 
 * with the given sector count. It then loads the correct driver
 * and installs it */
__EXTERN OsStatus_t DiskDetectFileSystem(FileSystemDisk_t *Disk,
    UUId_t BufferHandle, void* Buffer, uint64_t Sector, uint64_t SectorCount);

/* DiskDetectLayout
 * Detects the kind of layout on the disk, be it
 * MBR or GPT layout, if there is no layout it returns
 * OsError to indicate the entire disk is a FS */
__EXTERN OsStatus_t DiskDetectLayout(FileSystemDisk_t *Disk);

/* VfsResolveFileSystem
 * Tries to resolve the given filesystem by locating
 * the appropriate driver library for the given type */
__EXTERN FileSystemModule_t *VfsResolveFileSystem(FileSystem_t *FileSystem);

/* VfsGetResolverQueue
 * Retrieves a list of all the current filesystems
 * that needs to be resolved, and is scheduled */
__EXTERN Collection_t *VfsGetResolverQueue(void);

/* VfsGetFileSystems
 * Retrieves a list of all the current filesystems
 * and provides access for manipulation */
__EXTERN Collection_t *VfsGetFileSystems(void);

/* VfsGetModules
 * Retrieves a list of all the currently loaded
 * modules, provides access for manipulation */
__EXTERN Collection_t *VfsGetModules(void);

/* VfsGetDisks
 * Retrieves a list of all the currently registered
 * disks, provides access for manipulation */
__EXTERN Collection_t *VfsGetDisks(void);

/* VfsIdentifierFileGet
 * Retrieves a new identifier for a file-handle that
 * is system-wide unique */
__EXTERN UUId_t VfsIdentifierFileGet(void);

/* VfsGetOpenFiles / VfsGetOpenHandles
 * Retrieves the list of open files /handles and allows
 * access and manipulation of the list */
__EXTERN Collection_t* VfsGetOpenFiles(void);
__EXTERN Collection_t* VfsGetOpenHandles(void);

/* VfsIdentifierAllocate 
 * Allocates a free identifier index for the
 * given disk, it varies based upon disk type */
__EXTERN UUId_t
VfsIdentifierAllocate(
    _In_ FileSystemDisk_t* Disk);

/* VfsIdentifierFree 
 * Frees a given disk identifier index */
__EXTERN OsStatus_t
VfsIdentifierFree(
    _In_ FileSystemDisk_t* Disk,
    _In_ UUId_t            Id);

#endif //!_VFS_INTERFACE_H_
