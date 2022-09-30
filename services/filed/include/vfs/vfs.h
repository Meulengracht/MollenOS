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
#include <os/osdefs.h>

struct VFS;
struct VFSStorageParameters;
struct VFSInterface;
struct VFSNode;
struct VFSNodeHandle;
struct VFSRequest;
struct VFSStat;
struct VFSStatFS;

extern void VFSNodeHandleInitialize(void);

extern oserr_t VFSNew(uuid_t id, guid_t* guid, struct VFSInterface*, struct VFSStorageParameters*, struct VFS**);
extern oserr_t VFSChildNew(struct VFS*, struct VFS**);
extern void    VFSDestroy(struct VFS*);

extern oserr_t    VFSNodeNewDirectory(struct VFS*, mstring_t* path, uint32_t permissions, struct VFSNode**);
extern oserr_t    VFSNodeChildNew(struct VFS*, struct VFSNode*, struct VFSStat*, struct VFSNode**);
extern void       VFSNodeDestroy(struct VFSNode*);
extern mstring_t* VFSNodeMakePath(struct VFSNode* node, int local);

extern oserr_t VFSNodeOpen(struct VFS*, const char* cpath, uint32_t options, uint32_t access, uuid_t* handleOut);
extern oserr_t VFSNodeClose(struct VFS*, uuid_t handleID);
extern oserr_t VFSNodeLink(struct VFS*, struct VFSRequest*);
extern oserr_t VFSNodeUnlink(struct VFS*, struct VFSRequest*);
extern oserr_t VFSNodeMove(struct VFS*, struct VFSRequest*);
extern oserr_t VFSNodeStat(struct VFS*, struct VFSRequest*, struct VFSStat*);
extern oserr_t VFSNodeStatFs(struct VFS*, struct VFSRequest*, struct VFSStatFS*);
extern oserr_t VFSNodeStatStorage(struct VFS*, struct VFSRequest*, StorageDescriptor_t*);
extern oserr_t VFSNodeReadLink(struct VFS*, struct VFSRequest*, mstring_t**);
extern oserr_t VFSNodeRealPath(struct VFS*, struct VFSRequest*, mstring_t**);

extern oserr_t VFSNodeBind(struct VFS*, uuid_t fromID, uuid_t toID);
extern oserr_t VFSNodeUnbind(struct VFS*, uuid_t directoryHandleID);

extern oserr_t VFSNodeMount(struct VFS*, uuid_t atID, struct VFS* what);
extern oserr_t VFSNodeUnmount(struct VFS*, uuid_t directoryHandleID);

extern oserr_t VFSNodeDuplicate(struct VFSRequest*, uuid_t* handleOut);
extern oserr_t VFSNodeRead(struct VFSRequest*, size_t* readOut);
extern oserr_t VFSNodeReadAt(struct VFSRequest*, size_t* readOut);
extern oserr_t VFSNodeWrite(struct VFSRequest*, size_t* writtenOut);
extern oserr_t VFSNodeWriteAt(struct VFSRequest*, size_t* writtenOut);
extern oserr_t VFSNodeSeek(struct VFSRequest*, uint64_t* positionOut);
extern oserr_t VFSNodeFlush(struct VFSRequest*);

extern oserr_t VFSNodeGetPosition(struct VFSRequest*, uint64_t* positionOut);
extern oserr_t VFSNodeGetAccess(struct VFSRequest*, uint32_t* accessKindOut);
extern oserr_t VFSNodeSetAccess(struct VFSRequest*);
extern oserr_t VFSNodeGetSize(struct VFSRequest*, uint64_t* sizeOut);
extern oserr_t VFSNodeSetSize(struct VFSRequest*);
extern oserr_t VFSNodeStatHandle(struct VFSRequest*, struct VFSStat*);
extern oserr_t VFSNodeStatFsHandle(struct VFSRequest*, struct VFSStatFS*);
extern oserr_t VFSNodeStatStorageHandle(struct VFSRequest*, StorageDescriptor_t*);
extern oserr_t VFSNodeGetPathHandle(struct VFSRequest*, mstring_t**);

#endif //!__VFS_H__
