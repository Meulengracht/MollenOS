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

#define __need_quantity
#include <ddk/utils.h>
#include <os/handle.h>
#include <os/shm.h>
#include <vfs/vfs.h>
#include "../private.h"

static mstring_t* __SubtractPath(mstring_t* path, mstring_t* operand)
{
    if (mstr_cmp(path, operand)) {
        return NULL;
    }
    return mstr_substr(path, (int)mstr_len(operand), -1);
}

static mstring_t* __CombineNodePath(struct VFSNode* directoryNode, mstring_t* name)
{
    mstring_t* nodePath = VFSNodeMakePath(directoryNode, 1);
    mstring_t* combined;
    if (mstr_at(nodePath, -1) != '/') {
        combined = mstr_fmt("%ms/%ms", nodePath, name);
    } else {
        combined = mstr_fmt("%ms%ms", nodePath, name);
    }
    mstr_delete(nodePath);
    return combined;
}

static oserr_t __MoveLocal(struct VFSNode* sourceNode, struct VFSNode* directoryNode, mstring_t* targetName, int copy)
{
    struct VFSOperations* ops        = &sourceNode->FileSystem->Interface->Operations;
    struct VFS*           vfs        = sourceNode->FileSystem;
    mstring_t*            sourcePath = VFSNodeMakePath(sourceNode, 1);
    mstring_t*            targetPath = __CombineNodePath(directoryNode, targetName);
    oserr_t               oserr;

    if (sourcePath == NULL || targetPath == NULL) {
        mstr_delete(sourcePath);
        mstr_delete(targetPath);
        return OS_EOOM;
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
    struct __VFSChild* result = hashtable_get(
            &directoryNode->Children,
            &(struct __VFSChild) { .Key = targetName }
    );
    if (result != NULL) {
        oserr = OS_EEXISTS;
        goto unlock;
    }

    oserr = ops->Move(sourceNode->FileSystem->Interface, vfs->Data, sourcePath, targetPath, copy);
    if (oserr != OS_EOK) {
        goto unlock;
    }

    struct VFSNode* child;
    oserr = VFSNodeChildNew(vfs, directoryNode, &(struct VFSStat) {
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

    mstr_delete(sourcePath);
    mstr_delete(targetPath);
    return oserr;
}

static oserr_t __TransferFile(struct VFS* sourceVFS, void* sourceFile, struct VFS* targetVFS, void* targetFile)
{
    OSHandle_t shm;
    oserr_t    oserr;

    oserr = SHMCreate(
            &(SHM_t) {
                .Flags = SHM_DEVICE,
                .Conformity = OSMEMORYCONFORMITY_LOW,
                .Size = MB(1),
                .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
            },
            &shm
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    while (1) {
        size_t read, written;

        oserr = sourceVFS->Interface->Operations.Read(
                sourceVFS->Interface,
                sourceVFS->Data,
                sourceFile,
                shm.ID,
                SHMBuffer(&shm),
                0,
                MB(1), &read
        );
        if (oserr != OS_EOK || read == 0) {
            break;
        }

        oserr = targetVFS->Interface->Operations.Write(
                sourceVFS->Interface,
                targetVFS->Data, targetFile,
                shm.ID,
                SHMBuffer(&shm),
                0,
                read, &written
        );
        if (oserr != OS_EOK) {
            break;
        }
    }

    OSHandleDestroy(&shm);
    return OS_EOK;
}

static oserr_t __MoveCross(struct VFSNode* sourceNode, struct VFSNode* directoryNode, mstring_t* targetName, int copy)
{
    mstring_t* sourcePath = VFSNodeMakePath(sourceNode, 1);
    mstring_t* targetPath = __CombineNodePath(directoryNode, targetName);
    oserr_t osStatus, osStatus2;

    if (sourcePath == NULL || targetPath == NULL) {
        mstr_delete(sourcePath);
        mstr_delete(targetPath);
        return OS_EOOM;
    }

    struct VFSOperations* sourceOps = &sourceNode->FileSystem->Interface->Operations;
    void*                 sourceFile;
    osStatus = sourceOps->Open(
            sourceNode->FileSystem->Interface,
            sourceNode->FileSystem->Data,
            sourceNode->Name,
            &sourceFile
    );
    if (osStatus != OS_EOK) {
        goto cleanup;
    }

    // Now do the promotion to write for the directory node
    usched_rwlock_w_promote(&directoryNode->Lock);
    struct __VFSChild* result = hashtable_get(&directoryNode->Children, &(struct __VFSChild) { .Key = targetName });
    if (result != NULL) {
        osStatus = OS_EEXISTS;
        goto unlock;
    }

    struct VFSOperations* targetOps = &directoryNode->FileSystem->Interface->Operations;
    void*                 targetDirectory;
    osStatus = targetOps->Open(
            directoryNode->FileSystem->Interface,
            directoryNode->FileSystem->Data,
            directoryNode->Name,
            &targetDirectory
    );
    if (osStatus != OS_EOK) {
        goto unlock;
    }

    void* targetFile;
    osStatus = targetOps->Create(
            directoryNode->FileSystem->Interface,
            directoryNode->FileSystem->Data,
            targetDirectory,
            targetName,
            0,
            sourceNode->Stats.Flags,
            sourceNode->Stats.Permissions,
            &targetFile
    );
    if (osStatus != OS_EOK) {
        goto unlock;
    }

    osStatus2 = targetOps->Close(
            directoryNode->FileSystem->Interface,
            directoryNode->FileSystem->Data,
            targetDirectory
    );
    if (osStatus2 != OS_EOK) {
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
    if (osStatus != OS_EOK) {
        WARNING("__MoveCross failed to create new directory node %u", osStatus);
    }

    osStatus = __TransferFile(
            sourceNode->FileSystem, sourceFile,
            directoryNode->FileSystem, targetFile
    );
    osStatus2 = targetOps->Close(
            directoryNode->FileSystem->Interface,
            directoryNode->FileSystem->Data,
            targetFile
    );
    if (osStatus2 != OS_EOK) {
        WARNING("__MoveCross failed to cleanup handle with code %u", osStatus);
    }

unlock:
    usched_rwlock_w_demote(&directoryNode->Lock);
    osStatus2 = sourceOps->Close(
            sourceNode->FileSystem->Interface,
            sourceNode->FileSystem->Data,
            sourceFile
    );
    if (osStatus2 != OS_EOK) {
        WARNING("__MoveCross failed to cleanup handle with code %u", osStatus);
    }
    if (!copy) {
        osStatus2 = sourceOps->Unlink(
                sourceNode->FileSystem->Interface,
                sourceNode->FileSystem->Data,
                sourcePath
        );
        if (osStatus2 != OS_EOK) {
            WARNING("__MoveCross failed to delete original file with code %u", osStatus);
        }
    }

cleanup:
    mstr_delete(sourcePath);
    mstr_delete(targetPath);
    return osStatus;
}

oserr_t VFSNodeMove(struct VFS* vfs, const char* cfrom, const char* cto, bool copy)
{
    struct VFSNode* from;
    mstring_t*      fromPath = mstr_path_new_u8(cfrom);
    struct VFSNode* to;
    mstring_t*      toPath = mstr_path_new_u8(cto);
    mstring_t*      path = NULL;
    mstring_t*      targetName;
    oserr_t         osStatus;

    if (fromPath == NULL || toPath == NULL) {
        mstr_delete(fromPath);
        mstr_delete(toPath);
        return OS_EOOM;
    }

    // Before we do a move, we first want to make sure the source node exists,
    // but based on the filesystem of the source and the target destination we might
    // be able to do an internal copy instead of a proper copy.
    osStatus = VFSNodeGet(vfs, fromPath, 1, &from);
    if (osStatus != OS_EOK) {
        goto cleanup;
    }

    // Move only supports regular files, so get the directory node
    // of the target, which must exist
    path = mstr_path_dirname(toPath);
    if (path == NULL) {
        VFSNodePut(from);
        osStatus = OS_EOOM;
        goto cleanup;
    }

    osStatus = VFSNodeGet(vfs, path, 1, &to);
    if (osStatus != OS_EOK) {
        VFSNodePut(from);
        goto cleanup;
    }

    // Now we have both nodes, and can now do an informed decision
    targetName = __SubtractPath(toPath, path);
    if (from->FileSystem == to->FileSystem) {
        osStatus = __MoveLocal(from, to, targetName, copy);
    } else {
        osStatus = __MoveCross(from, to, targetName, copy);
    }

    VFSNodePut(to);
    VFSNodePut(from);

cleanup:
    mstr_delete(fromPath);
    mstr_delete(toPath);
    mstr_delete(path);
    return osStatus;
}
