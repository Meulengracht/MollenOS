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

enum VFSNodeType {
    VFS_NODE_DEVICE,
    VFS_NODE_FILESYSTEM,
    VFS_NODE_DIRECTORY,
};

extern OsStatus_t VFSNew(struct VFS**);
extern OsStatus_t VFSChildNew(struct VFS*, struct VFS**);
extern OsStatus_t VFSDestroy(struct VFS*);

extern OsStatus_t VFSNodeNew(struct VFS*, MString_t* path, enum VFSNodeType, struct VFSNode**);
extern OsStatus_t VFSNodeChildNew(struct VFS*, struct VFSNode*, MString_t* name, enum VFSNodeType, struct VFSNode**);
extern OsStatus_t VFSNodeDestroy(struct VFS*, struct VFSNode*);

extern OsStatus_t       VFSNodeDataSet(struct VFSNode*, const void*);
extern const void*      VFSNodeDataGet(struct VFSNode*);
extern MString_t*       VFSNodePath(struct VFSNode*);
extern enum VFSNodeType VFSNodeType(struct VFSNode*);

extern OsStatus_t VFSNodeBind(struct VFS*, struct VFSNode* from, struct VFSNode* to);
extern OsStatus_t VFSNodeUnbind(struct VFS*, struct VFSNode*);

extern OsStatus_t VFSNodeChildCount(struct VFS*, struct VFSNode*);
extern OsStatus_t VFSNodeChildGetIndex(struct VFS*, struct VFSNode*, int);
extern OsStatus_t VFSNodeLookup(struct VFS*, MString_t* path, struct VFSNode**);

#endif //!__VFS_H__
