/**
 * Copyright 2021, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __VFS_H__
#define __VFS_H__

#include <ddk/storage.h>
#include <ds/guid.h>
#include <ds/mstring.h>

struct VFS;
struct VFSStorage;
struct VFSStorageParameters;
struct VFSInterface;
struct VFSNode;
struct VFSNodeHandle;
struct VFSRequest;
struct VFSStat;
struct VFSStatFS;

extern void VFSNodeHandleStartup(void);

extern oserr_t
VFSNew(
        _In_  uuid_t                id,
        _In_  guid_t*               guid,
        _In_  struct VFSStorage*    storage,
        _In_  struct VFSInterface*  interface,
        _In_  void*                 data,
        _Out_ struct VFS**          vfsOut);

extern oserr_t VFSChildNew(struct VFS*, struct VFS**);
extern void    VFSDestroy(struct VFS*);

extern oserr_t    VFSNodeNewDirectory(struct VFS*, mstring_t* path, uint32_t permissions, struct VFSNode**);
extern oserr_t    VFSNodeChildNew(struct VFS*, struct VFSNode*, struct VFSStat*, struct VFSNode**);
extern void       VFSNodeDestroy(struct VFSNode*);
extern mstring_t* VFSNodeMakePath(struct VFSNode* node, int local);

extern oserr_t VFSNodeOpen(struct VFS*, const char* cpath, uint32_t options, uint32_t permissions, uuid_t* handleOut);
extern oserr_t VFSNodeClose(struct VFS*, uuid_t handleID);
extern oserr_t VFSNodeMove(struct VFS*, const char* cfrom, const char* cto, bool copy);
extern oserr_t VFSNodeStat(struct VFS*, const char* cpath, int followLinks, struct VFSStat*);
extern oserr_t VFSNodeStatFs(struct VFS*, const char* cpath, int followLinks, struct VFSStatFS*);
extern oserr_t VFSNodeStatStorage(struct VFS*, const char* cpath, int followLinks, StorageDescriptor_t*);
extern oserr_t VFSNodeRealPath(struct VFS*, const char* cpath, int followLink, mstring_t**);

extern oserr_t VFSNodeLink(struct VFS*, const char* cfrom, const char* cto, bool symbolic);
extern oserr_t VFSNodeUnlink(struct VFS*, const char* cpath);
extern oserr_t VFSNodeReadLink(struct VFS*, const char* cpath, mstring_t**);

extern oserr_t VFSNodeMkdir(struct VFS*, mstring_t* path, uint32_t access, uuid_t* handleOut);
extern oserr_t VFSNodeReadDirectory(uuid_t fileHandle, struct VFSStat* stats, uint32_t* indexOut);

extern oserr_t VFSNodeBind(struct VFS*, uuid_t fromID, uuid_t toID);
extern oserr_t VFSNodeUnbind(struct VFS*, uuid_t directoryHandleID);

extern oserr_t VFSNodeMount(struct VFS*, uuid_t atID, struct VFS* what);
extern oserr_t VFSNodeUnmount(struct VFS*, uuid_t directoryHandleID);
extern oserr_t VFSNodeUnmountPath(struct VFS*, mstring_t* path);

extern oserr_t VFSNodeDuplicate(uuid_t handle, uuid_t* handleOut);
extern oserr_t VFSNodeRead(uuid_t fileHandle, uuid_t bufferHandle, size_t offset, size_t length, size_t* readOut);
extern oserr_t VFSNodeReadAt(uuid_t fileHandle, UInteger64_t* position, uuid_t bufferHandle, size_t offset, size_t length, size_t* readOut);
extern oserr_t VFSNodeWrite(uuid_t fileHandle, uuid_t bufferHandle, size_t offset, size_t length, size_t* writtenOut);
extern oserr_t VFSNodeWriteAt(uuid_t fileHandle, UInteger64_t* position, uuid_t bufferHandle, size_t offset, size_t length, size_t* writtenOut);
extern oserr_t VFSNodeSeek(uuid_t fileHandle, UInteger64_t* position, uint64_t* positionOut);
extern oserr_t VFSNodeFlush(uuid_t fileHandle);

extern oserr_t VFSNodeGetPosition(uuid_t fileHandle, uint64_t* positionOut);
extern oserr_t VFSNodeGetAccess(uuid_t fileHandle, uint32_t* accessKindOut);
extern oserr_t VFSNodeSetAccess(uuid_t fileHandle, uint32_t access);
extern oserr_t VFSNodeGetSize(uuid_t fileHandle, uint64_t* sizeOut);
extern oserr_t VFSNodeSetSize(uuid_t fileHandle, UInteger64_t* size);
extern oserr_t VFSNodeStatHandle(uuid_t fileHandle, struct VFSStat*);
extern oserr_t VFSNodeStatFsHandle(uuid_t fileHandle, struct VFSStatFS*);
extern oserr_t VFSNodeStatStorageHandle(uuid_t fileHandle, StorageDescriptor_t*);
extern oserr_t VFSNodeGetPathHandle(uuid_t handleID, mstring_t**);

#endif //!__VFS_H__
