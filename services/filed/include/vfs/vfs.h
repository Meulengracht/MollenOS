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

#include <os/osdefs.h>
#include <ddk/storage.h>
#include <ds/mstring.h>

struct VFS;
struct VFSNode;
struct VFSNodeHandle;
struct VFSRequest;
struct VFSStat;
struct VFSStatFS;

extern OsStatus_t VFSNew(struct VFSOperations*, struct VFS**);
extern OsStatus_t VFSChildNew(struct VFS*, struct VFS**);
extern OsStatus_t VFSDestroy(struct VFS*);

extern OsStatus_t VFSNodeMount(struct VFS*, struct VFS*, MString_t* path);
extern OsStatus_t VFSNodeUnmount(struct VFS*, MString_t* path);

extern OsStatus_t VFSNodeBind(struct VFS*, struct VFSNode* from, struct VFSNode* to);
extern OsStatus_t VFSNodeUnbind(struct VFS*, struct VFSNode*);

extern OsStatus_t VFSNodeNew(struct VFS*, MString_t* path, enum VFSNodeType, struct VFSNode**);
extern OsStatus_t VFSNodeChildNew(struct VFS*, struct VFSNode*, struct VFSStat*, struct VFSNode**);
extern void VFSNodeDestroy(struct VFS*, struct VFSNode*);
extern OsStatus_t VFSNodeLookup(struct VFS*, MString_t* path, struct VFSNode**);

extern OsStatus_t VFSNodeOpen(struct VFS*, struct VFSRequest*, UUId_t* handleOut);
extern OsStatus_t VFSNodeClose(struct VFS*, struct VFSRequest*);
extern OsStatus_t VFSNodeLink(struct VFS*, struct VFSRequest*);
extern OsStatus_t VFSNodeUnlink(struct VFS*, struct VFSRequest*);
extern OsStatus_t VFSNodeMove(struct VFS*, struct VFSRequest*);
extern OsStatus_t VFSNodeStat(struct VFS*, struct VFSRequest*, struct VFSStat*);
extern OsStatus_t VFSNodeStatFs(struct VFS*, struct VFSRequest*, struct VFSStatFS*);
extern OsStatus_t VFSNodeStatStorage(struct VFS*, struct VFSRequest*, StorageDescriptor_t*);
extern OsStatus_t VFSNodeRealPath(struct VFS*, struct VFSRequest*, MString_t**);

extern OsStatus_t VFSNodeDuplicate(struct VFS*, struct VFSRequest*, UUId_t* handleOut);
extern OsStatus_t VFSNodeRead(struct VFS*, struct VFSRequest*);
extern OsStatus_t VFSNodeReadAt(struct VFS*, struct VFSRequest*);
extern OsStatus_t VFSNodeWrite(struct VFS*, struct VFSRequest*);
extern OsStatus_t VFSNodeWriteAt(struct VFS*, struct VFSRequest*);
extern void VFSNodeSeek(struct VFS*, struct VFSRequest*);
extern void VFSNodeFlush(struct VFS*, struct VFSRequest*);

extern OsStatus_t VFSNodeGetPosition(struct VFSRequest*, uint64_t* positionOut);
extern OsStatus_t VFSNodeGetAccess(struct VFSRequest*, uint32_t* accessKindOut);
extern OsStatus_t VFSNodeSetAccess(struct VFSRequest*);
extern OsStatus_t VFSNodeGetSize(struct VFSRequest*, uint64_t* sizeOut);
extern OsStatus_t VFSNodeSetSize(struct VFSRequest*);
extern OsStatus_t VFSNodeStatHandle(struct VFSRequest*, struct VFSStat*);
extern OsStatus_t VFSNodeStatFsHandle(struct VFSRequest*, struct VFSStatFS*);
extern OsStatus_t VFSNodeStatStorageHandle(struct VFSRequest*, StorageDescriptor_t*);
extern OsStatus_t VFSNodeGetPathHandle(struct VFSRequest*, MString_t**);

#endif //!__VFS_H__
