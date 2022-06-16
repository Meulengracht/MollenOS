/**
 * Copyright 2022, Philip Meulengracht
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

#include <ddk/utils.h>
#include <vfs/vfs.h>

void VFSNodeOpen(struct VFS* vfs, struct VFSRequest* request)
{
    
}

extern void VFSNodeClose(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeDelete(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeMove(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeLink(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeStat(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeStatFs(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeStatStorage(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeRealPath(struct VFS* vfs, struct VFSRequest* request);

extern void VFSNodeRead(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeReadAt(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeWrite(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeWriteAt(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeSeek(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeFlush(struct VFS* vfs, struct VFSRequest* request);

extern void VFSNodeGetPosition(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeGetOptions(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeSetOptions(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeGetSize(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeSetSize(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeStatHandle(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeStatFsHandle(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeStatStorageHandle(struct VFS* vfs, struct VFSRequest* request);
extern void VFSNodeGetPathHandle(struct VFS*, s