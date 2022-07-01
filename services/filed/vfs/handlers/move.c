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

#include <os/dmabuf.h>
#include <ddk/utils.h>
#include <vfs/requests.h>
#include <vfs/vfs.h>
#include "../private.h"

static MString_t* __DirectoryOf(MString_t* path)
{
    size_t pathLength = MStringLength(path);
    int    lastOccurrence;

    lastOccurrence = MStringFindReverse(path, '/', 0);
    if (lastOccurrence == (int)pathLength) {
        lastOccurrence = MStringFindReverse(path, '/', lastOccurrence);
    }
    return MStringSubString(path, 0, lastOccurrence);
}

static MString_t* __SubtractPath(MString_t* path, MString_t* operand)
{
    int result = MStringCompare(path, operand, 0);
    if (result == MSTRING_NO_MATCH) {
        return NULL;
    }
    return MStringSubString(path, (int)MStringLength(operand), -1);
}

static MString_t* __CombineNodePath(struct VFSNode* directoryNode, MString_t* name)
{
    MString_t* nodePath = VFSNodeMakePath(directoryNode, 1);
    if (MStringGetCharAt(nodePath, (int)MStringLength(nodePath) - 1) != '/') {
        MStringAppendCharacter(nodePath, '/');
    }
    MStringAppend(nodePath, name);
    return nodePath;
}

static OsStatus_t __MoveLocal(struct VFSNode* sourceNode, struct VFSNode* directoryNode, MString_t* targetName, int copy)
{
    struct VFSOperations* ops        = &sourceNode->FileSystem->Module->Operations;
    struct VFS*           vfs        = sourceNode->FileSystem;
    MString_t*            sourcePath = VFSNodeMakePath(sourceNode, 1);
    MString_t*            targetPath = __CombineNodePath(directoryNode, targetName);
    OsStatus_t            osStatus;

    if (sourcePath == NULL || targetPath == NULL) {
        MStringDestroy(sourcePath);
        MStringDestroy(targetPath);
        return OsOutOfMemory;
    }

    // Get write access, the issue here is that actually at this point we _may_
    // hold two reader lock if the paths are identical. This makes it impossible
    // to use rwlock_w_promote as it only takes care of one unlock. There is only
    // one case where we have to handle this, and that is if the source node (going back)
    // intersects with the directory node passed
    bool            unlockFirst = false;
    struct VFSNode* i           = sourceNode->Parent; // source node refers to a file, so take parent
    while (i) {
        if (i == directoryNode) {
            unlockFirst = true;
            break;
        }
    }

    if (unlockFirst) {
        usched_rwlock_r_unlock(&directoryNode->Lock);
    }

    // Now do the promotion to write for the directory node
    usched_rwlock_w_promote(&directoryNode->Lock);
    struct __VFSChild* result = hashtable_get(&directoryNode->Children, &(struct __VFSChild) { .Key = targetName });
    if (result != NULL) {
        osStatus = OsExists;
        goto unlock;
    }

    osStatus = ops->Move(&vfs->Base, sourcePath, targetPath, copy);
    if (osStatus != OsOK) {
        goto unlock;
    }

    struct VFSNode* child;
    osStatus = VFSNodeChildNew(vfs, directoryNode, &(struct VFSStat) {
            .Name = targetName,
            .Size = sourceNode->Stats.Size,
            .Owner = 0,
            .Flags = sourceNode->Stats.Flags,
            .Permissions = sourceNode->Stats.Permissions
    }, &child);

unlock:
    usched_rwlock_w_demote(&directoryNode->Lock);
    if (unlockFirst) {
        usched_rwlock_r_lock(&directoryNode->Lock);
    }

    MStringDestroy(sourcePath);
    MStringDestroy(targetPath);
    return osStatus;
}

static OsStatus_t __TransferFile(struct VFS* sourceVFS, void* sourceFile, struct VFS* targetVFS, void* targetFile)
{
    struct dma_buffer_info buffer;
    struct dma_attachment  attachment;
    OsStatus_t             osStatus;

    buffer.name     = "vfs_transfer_file";
    buffer.flags    = 0;
    buffer.type     = DMA_TYPE_DRIVER_32;
    buffer.length   = MB(1);
    buffer.capacity = MB(1);
    osStatus = dma_create(&buffer, &attachment);
    if (osStatus != OsOK) {
        return osStatus;
    }

    while (1) {
        size_t read, written;

        osStatus = sourceVFS->Module->Operations.Read(&sourceVFS->Base, sourceFile,
                                                      attachment.handle, attachment.buffer, 0,
                                                      MB(1), &read);
        if (osStatus != OsOK || read == 0) {
            break;
        }

        osStatus = targetVFS->Module->Operations.Write(&targetVFS->Base, targetFile,
                                                       attachment.handle, attachment.buffer, 0,
                                                       read, &written);
        if (osStatus != OsOK) {
            break;
        }
    }

    dma_detach(&attachment);
    return OsOK;
}

static OsStatus_t __MoveCross(struct VFSNode* sourceNode, struct VFSNode* directoryNode, MString_t* targetName, int copy)
{
    MString_t* sourcePath = VFSNodeMakePath(sourceNode, 1);
    MString_t* targetPath = __CombineNodePath(directoryNode, targetName);
    OsStatus_t osStatus, osStatus2;

    if (sourcePath == NULL || targetPath == NULL) {
        MStringDestroy(sourcePath);
        MStringDestroy(targetPath);
        return OsOutOfMemory;
    }

    struct VFSOperations* sourceOps = &sourceNode->FileSystem->Module->Operations;
    void*                 sourceFile;
    osStatus = sourceOps->Open(&sourceNode->FileSystem->Base, sourceNode->Name, &sourceFile);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    // Now do the promotion to write for the directory node
    usched_rwlock_w_promote(&directoryNode->Lock);
    struct __VFSChild* result = hashtable_get(&directoryNode->Children, &(struct __VFSChild) { .Key = targetName });
    if (result != NULL) {
        osStatus = OsExists;
        goto unlock;
    }

    struct VFSOperations* targetOps = &directoryNode->FileSystem->Module->Operations;
    void*                 targetDirectory;
    osStatus = targetOps->Open(&directoryNode->FileSystem->Base, directoryNode->Name, &targetDirectory);
    if (osStatus != OsOK) {
        goto unlock;
    }

    void* targetFile;
    osStatus = targetOps->Create(&directoryNode->FileSystem->Base, targetDirectory, targetName, 0,
                                 sourceNode->Stats.Flags, sourceNode->Stats.Permissions, &targetFile);
    if (osStatus != OsOK) {
        goto unlock;
    }

    osStatus2 = targetOps->Close(&directoryNode->FileSystem->Base, targetDirectory);
    if (osStatus2 != OsOK) {
        WARNING("__MoveCross failed to cleanup handle with code %u", osStatus);
    }

    struct VFSNode* child;
    osStatus = VFSNodeChildNew(directoryNode->FileSystem, directoryNode, &(struct VFSStat) {
            .Name = targetName,
            .Size = sourceNode->Stats.Size,
            .Owner = 0,
            .Flags = sourceNode->Stats.Flags,
            .Permissions = sourceNode->Stats.Permissions
    }, &child);
    if (osStatus != OsOK) {
        WARNING("__MoveCross failed to create new directory node %u", osStatus);
    }

    osStatus = __TransferFile(sourceNode->FileSystem, sourceFile,
                              directoryNode->FileSystem, targetFile);
    osStatus2 = targetOps->Close(&directoryNode->FileSystem->Base, targetFile);
    if (osStatus2 != OsOK) {
        WARNING("__MoveCross failed to cleanup handle with code %u", osStatus);
    }

unlock:
    usched_rwlock_w_demote(&directoryNode->Lock);
    osStatus2 = sourceOps->Close(&sourceNode->FileSystem->Base, sourceFile);
    if (osStatus2 != OsOK) {
        WARNING("__MoveCross failed to cleanup handle with code %u", osStatus);
    }

cleanup:
    MStringDestroy(sourcePath);
    MStringDestroy(targetPath);
    return osStatus;
}

OsStatus_t VFSNodeMove(struct VFS* vfs, struct VFSRequest* request)
{
    struct VFSNode* from;
    MString_t*      fromPath = VFSMakePath(request->parameters.move.from);
    struct VFSNode* to;
    MString_t*      toPath = VFSMakePath(request->parameters.move.to);
    MString_t*      path;
    MString_t*      targetName;
    OsStatus_t      osStatus;

    if (fromPath == NULL || toPath == NULL) {
        MStringDestroy(fromPath);
        MStringDestroy(toPath);
        return OsOutOfMemory;
    }

    // Before we do a move, we first want to make sure the source node exists,
    // but based on the filesystem of the source and the target destination we might
    // be able to do an internal copy instead of a proper copy.
    osStatus = VFSNodeGet(vfs, fromPath, 1, &from);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    // Move only supports regular files, so get the directory node
    // of the target, which must exist
    path = __DirectoryOf(toPath);
    if (path == NULL) {
        VFSNodePut(from);
        osStatus = OsOutOfMemory;
        goto cleanup;
    }

    osStatus = VFSNodeGet(vfs, path, 1, &to);
    if (osStatus != OsOK) {
        VFSNodePut(from);
        goto cleanup;
    }

    // Now we have both nodes, and can now do an informed decision
    targetName = __SubtractPath(toPath, path);
    if (from->FileSystem == to->FileSystem) {
        osStatus = __MoveLocal(from, to, targetName, request->parameters.move.copy);
    } else {
        osStatus = __MoveCross(from, to, targetName, request->parameters.move.copy);
    }

    VFSNodePut(to);
    VFSNodePut(from);

cleanup:
    MStringDestroy(fromPath);
    MStringDestroy(toPath);
    return osStatus;
}
