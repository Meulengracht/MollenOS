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
#include <ds/mstring.h>

struct VFS;
struct VFSNode;
struct VFSRequest;

enum VFSNodeType {
    VFS_NODE_DEVICE,
    VFS_NODE_DIRECTORY,
};

struct VFSOperations {
    OsStatus_t (*Initialize)(void);
    OsStatus_t (*Destroy)(void);

    OsStatus_t (*Open)(void);

};

extern OsStatus_t VFSNew(struct VFSOperations*, struct VFS**);
extern OsStatus_t VFSChildNew(struct VFS*, struct VFS**);
extern OsStatus_t VFSDestroy(struct VFS*);

extern OsStatus_t VFSNodeMount(struct VFS*, struct VFS*, MString_t* path);
extern OsStatus_t VFSNodeUnmount(struct VFS*, MString_t* path);

extern OsStatus_t VFSNodeBind(struct VFS*, struct VFSNode* from, struct VFSNode* to);
extern OsStatus_t VFSNodeUnbind(struct VFS*, struct VFSNode*);

extern void VFSNodeOpen(struct VFS*, struct VFSRequest*);
extern void VFSNodeClose(struct VFS*, struct VFSRequest*);
extern void VFSNodeDelete(struct VFS*, struct VFSRequest*);
extern void VFSNodeMove(struct VFS*, struct VFSRequest*);
extern void VFSNodeLink(struct VFS*, struct VFSRequest*);

extern void VFSNodeRead(struct VFS*, struct VFSRequest*);
extern void VFSNodeReadAt(struct VFS*, struct VFSRequest*);
extern void VFSNodeWrite(struct VFS*, struct VFSRequest*);
extern void VFSNodeWriteAt(struct VFS*, struct VFSRequest*);
extern void VFSNodeSeek(struct VFS*, struct VFSRequest*);
extern void VFSNodeFlush(struct VFS*, struct VFSRequest*);

extern void VFSNodeGetPosition(struct VFS*, struct VFSRequest*);
extern void VFSNodeGetOptions(struct VFS*, struct VFSRequest*);
extern void VFSNodeSetOptions(struct VFS*, struct VFSRequest*);
extern void VFSNodeGetSize(struct VFS*, struct VFSRequest*);
extern void VFSNodeSetSize(struct VFS*, struct VFSRequest*);
extern void VFSNodeStatHandle(struct VFS*, struct VFSRequest*);
extern void VFSNodeStatStorageHandle(struct VFS*, struct VFSRequest*);

extern OsStatus_t VFSNodeNew(struct VFS*, MString_t* path, enum VFSNodeType, struct VFSNode**);
extern OsStatus_t VFSNodeChildNew(struct VFS*, struct VFSNode*, MString_t* name, enum VFSNodeType, struct VFSNode**);
extern OsStatus_t VFSNodeDestroy(struct VFS*, struct VFSNode*);

extern OsStatus_t       VFSNodeDataSet(struct VFSNode*, const void*);
extern const void*      VFSNodeDataGet(struct VFSNode*);
extern MString_t*       VFSNodePath(struct VFSNode*);
extern enum VFSNodeType VFSNodeType(struct VFSNode*);

extern OsStatus_t VFSNodeChildCount(struct VFS*, struct VFSNode*);
extern OsStatus_t VFSNodeChildGetIndex(struct VFS*, struct VFSNode*, int);
extern OsStatus_t VFSNodeLookup(struct VFS*, MString_t* path, struct VFSNode**);

#endif //!__VFS_H__
