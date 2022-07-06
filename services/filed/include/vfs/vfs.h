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
struct VFSOperations;
struct VFSNode;
struct VFSNodeHandle;
struct VFSRequest;
struct VFSStat;
struct VFSStatFS;

extern oscode_t VFSNew(struct VFSOperations*, struct VFS**);
extern oscode_t VFSChildNew(struct VFS*, struct VFS**);
extern oscode_t VFSDestroy(struct VFS*);

extern oscode_t VFSNodeMount(struct VFS*, struct VFS*, MString_t* path);
extern oscode_t VFSNodeUnmount(struct VFS*, MString_t* path);

extern oscode_t VFSNodeBind(struct VFS*, struct VFSNode* from, struct VFSNode* to);
extern oscode_t VFSNodeUnbind(struct VFS*, struct VFSNode*);

extern oscode_t VFSNodeNew(struct VFS*, MString_t* path, enum VFSNodeType, struct VFSNode**);
extern oscode_t VFSNodeChildNew(struct VFS*, struct VFSNode*, struct VFSStat*, struct VFSNode**);
extern void       VFSNodeDestroy(struct VFS*, struct VFSNode*);
extern oscode_t VFSNodeLookup(struct VFS*, MString_t* path, struct VFSNode**);

extern oscode_t VFSNodeOpen(struct VFS*, struct VFSRequest*, UUId_t* handleOut);
extern oscode_t VFSNodeClose(struct VFS*, struct VFSRequest*);
extern oscode_t VFSNodeLink(struct VFS*, struct VFSRequest*);
extern oscode_t VFSNodeUnlink(struct VFS*, struct VFSRequest*);
extern oscode_t VFSNodeMove(struct VFS*, struct VFSRequest*);
extern oscode_t VFSNodeStat(struct VFS*, struct VFSRequest*, struct VFSStat*);
extern oscode_t VFSNodeStatFs(struct VFS*, struct VFSRequest*, struct VFSStatFS*);
extern oscode_t VFSNodeStatStorage(struct VFS*, struct VFSRequest*, StorageDescriptor_t*);
extern oscode_t VFSNodeReadLink(struct VFS*, struct VFSRequest*, MString_t**);
extern oscode_t VFSNodeRealPath(struct VFS*, struct VFSRequest*, MString_t**);

extern oscode_t VFSNodeDuplicate(struct VFSRequest*, UUId_t* handleOut);
extern oscode_t VFSNodeRead(struct VFSRequest*, size_t* readOut);
extern oscode_t VFSNodeReadAt(struct VFSRequest*, size_t* readOut);
extern oscode_t VFSNodeWrite(struct VFSRequest*, size_t* writtenOut);
extern oscode_t VFSNodeWriteAt(struct VFSRequest*, size_t* writtenOut);
extern oscode_t VFSNodeSeek(struct VFSRequest*, uint64_t* positionOut);
extern oscode_t VFSNodeFlush(struct VFSRequest*);

extern oscode_t VFSNodeGetPosition(struct VFSRequest*, uint64_t* positionOut);
extern oscode_t VFSNodeGetAccess(struct VFSRequest*, uint32_t* accessKindOut);
extern oscode_t VFSNodeSetAccess(struct VFSRequest*);
extern oscode_t VFSNodeGetSize(struct VFSRequest*, uint64_t* sizeOut);
extern oscode_t VFSNodeSetSize(struct VFSRequest*);
extern oscode_t VFSNodeStatHandle(struct VFSRequest*, struct VFSStat*);
extern oscode_t VFSNodeStatFsHandle(struct VFSRequest*, struct VFSStatFS*);
extern oscode_t VFSNodeStatStorageHandle(struct VFSRequest*, StorageDescriptor_t*);
extern oscode_t VFSNodeGetPathHandle(struct VFSRequest*, MString_t**);

#endif //!__VFS_H__
