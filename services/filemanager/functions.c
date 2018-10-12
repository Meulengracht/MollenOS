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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 * - ToDo Buffering is not ported to BufferObjects yet
 */
//#define __TRACE

#include <os/file.h>
#include <os/mollenos.h>
#include <os/utils.h>
#include "include/vfs.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* VfsEntryIsFile
 * Returns whether or not the given filesystem entry is a file. */
int
VfsEntryIsFile(
    _In_ FileSystemEntry_t* Entry)
{
    return (Entry->Descriptor.Flags & FILE_FLAG_DIRECTORY) == 0 ? 1 : 0;
}

/* VfsGetFileSystemFromPath
 * Retrieves the filesystem handle associated with the given path. */
FileSystem_t*
VfsGetFileSystemFromPath(
    _In_  MString_t*                Path,
    _Out_ MString_t**               SubPath)
{
    CollectionItem_t* Node;
    MString_t* Identifier;
    int Index;

    // To open a new file we need to find the correct
    // filesystem identifier and seperate it from it's absolute path
    Index = MStringFind(Path, ':', 0);
    if (Index == MSTRING_NOT_FOUND) {
        return NULL;
    }

    Identifier  = MStringSubString(Path, 0, Index);
    *SubPath    = MStringSubString(Path, Index + 2, -1);

    // Iterate all the filesystems and find the one
    // that matches
    _foreach(Node, VfsGetFileSystems()) {
        FileSystem_t *Filesystem = (FileSystem_t*)Node->Data;
        if (MStringCompare(Identifier, Filesystem->Identifier, 1)) {
            MStringDestroy(Identifier);
            return Filesystem;
        }
    }
    MStringDestroy(Identifier);
    MStringDestroy(*SubPath);
    return NULL;
}

/* VfsIsHandleValid
 * Checks for both owner permission and verification of the handle. */
FileSystemCode_t
VfsIsHandleValid(
    _In_  UUId_t                    Requester,
    _In_  UUId_t                    Handle,
    _In_  Flags_t                   RequiredAccess,
    _Out_ FileSystemEntryHandle_t** EntryHandle)
{
    CollectionItem_t *Node;
    DataKey_t Key;

    Key.Value   = (int)Handle;
    Node       = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);
    if (Node == NULL) {
        ERROR("Invalid handle given for file");
        return FsInvalidParameters;
    }

    *EntryHandle = (FileSystemEntryHandle_t*)Node->Data;
    if ((*EntryHandle)->Owner != Requester) {
        ERROR("Owner of the handle did not match the requester. Access Denied.");
        return FsAccessDenied;
    }

    if ((*EntryHandle)->Entry->IsLocked != UUID_INVALID && (*EntryHandle)->Entry->IsLocked != Requester) {
        ERROR("Entry is locked and lock is not held by requester. Access Denied.");
        return FsAccessDenied;
    }

    if (RequiredAccess != 0 && ((*EntryHandle)->Access & RequiredAccess) != RequiredAccess) {
        ERROR("Handle was not opened with the required access parameter. Access Denied.");
        return FsAccessDenied;
    }
    return FsOk;
}

/* VfsOpenHandleInternal
 * Internal helper for instantiating the entry handle
 * this does not take care of anything else than opening the handle */
FileSystemCode_t 
VfsOpenHandleInternal(
    _In_  FileSystemEntry_t*        Entry,
    _Out_ FileSystemEntryHandle_t** Handle)
{
    FileSystem_t *Filesystem = (FileSystem_t*)Entry->System;
    FileSystemCode_t Code;

    TRACE("VfsOpenHandleInternal()");

    Code = Filesystem->Module->OpenHandle(&Filesystem->Descriptor, Entry, Handle);
    if (Code != FsOk) {
        ERROR("Failed to initiate a new entry-handle, code %i", Code);
        return Code;
    }

    (*Handle)->LastOperation       = __FILE_OPERATION_NONE;
    (*Handle)->OutBuffer           = NULL;
    (*Handle)->OutBufferPosition   = 0;
    (*Handle)->Position            = 0;
    (*Handle)->Entry               = Entry;

    // Handle file specific options
    if (VfsEntryIsFile(Entry)) {
        // Initialise buffering as long as the file
        // handle is not opened as volatile
        if (!((*Handle)->Options & __FILE_VOLATILE)) {
            (*Handle)->OutBuffer = malloc(Filesystem->Descriptor.Disk.Descriptor.SectorSize);
            memset((*Handle)->OutBuffer, 0, Filesystem->Descriptor.Disk.Descriptor.SectorSize);
        }

        // Now comes the step where we handle options 
        // - but only options that are handle-specific
        if ((*Handle)->Options & __FILE_APPEND) {
            Code = Filesystem->Module->SeekInEntry(&Filesystem->Descriptor, (*Handle), Entry->Descriptor.Size.QuadPart);
        }
    }

    // Entry locked for access?
    if ((*Handle)->Access & __FILE_WRITE_ACCESS && !((*Handle)->Access & __FILE_WRITE_SHARE)) {
        Entry->IsLocked = (*Handle)->Owner;
    }
    return Code;
}

/* VfsVerifyAccessToPath
 * Verifies the requested user has access to the path. */
FileSystemCode_t
VfsVerifyAccessToPath(
    _In_  MString_t*                Path,
    _In_  Flags_t                   Options,
    _In_  Flags_t                   Access,
    _Out_ FileSystemEntry_t**       ExistingEntry)
{
    CollectionItem_t* Node;
    int PathHash = (int)MStringHash(Path);

    _foreach(Node, VfsGetOpenFiles()) {
        FileSystemEntry_t *Entry = (FileSystemEntry_t*)Node->Data;
        // If our requested mode is exclusive, then we must verify
        // none in our sub-path is opened in exclusive
        if (Access & __FILE_WRITE_ACCESS && !(Access & __FILE_WRITE_SHARE) &&
            Node->Key.Value != PathHash) {
            // Check if <Entry> contains the entirety of <Path>, if it does then deny
            // the request as we try to open a higher-level entry in exclusive mode
            if (MStringCompare(Entry->Path, Path, 0) != MSTRING_NO_MATCH) {
                ERROR("Entry is blocked from exclusive access, access denied.");
                return FsAccessDenied;
            }
        }

        // Have we found the existing already opened file?
        if (Node->Key.Value == PathHash) {
            if (Entry->IsLocked != UUID_INVALID) {
                ERROR("File is opened in exclusive mode already, access denied.");
                return FsAccessDenied;
            }
            else {
                // It's important here that we check if the flag
                // __FILE_FAILONEXIST has been set, then we return
                // the appropriate code instead of opening a new handle
                if (Options & __FILE_FAILONEXIST) {
                    ERROR("File already exists - open mode specifies this to be failure.");
                    return FsPathExists;
                }
                *ExistingEntry = Entry;
                break;
            }
        }
    }
    return FsOk;
}

/* VfsOpenInternal
 * Reusable helper for the VfsOpen to open internal
 * handles and performs the interaction with fs */
FileSystemCode_t 
VfsOpenInternal(
    _In_  MString_t*                Path,
    _In_  Flags_t                   Options,
    _In_  Flags_t                   Access,
    _Out_ FileSystemEntryHandle_t** Handle)
{
    FileSystemEntry_t* Entry    = NULL;
    MString_t* SubPath          = NULL;
    FileSystemCode_t Code;
    DataKey_t Key;

    TRACE("VfsOpenInternal(Path %s)", MStringRaw(Path));

    Code = VfsVerifyAccessToPath(Path, Options, Access, &Entry);
    if (Code == FsOk) {
        // Ok if it didn't exist in cache it's a new lookup
        if (Entry == NULL) {
            FileSystem_t *Filesystem    = VfsGetFileSystemFromPath(Path, &SubPath);
            int Created                 = 0;
            if (Filesystem == NULL) {
                return FsPathNotFound;
            }

            // Let the module do the rest
            Code = Filesystem->Module->OpenEntry(&Filesystem->Descriptor, SubPath, &Entry);
            if (Code == FsPathNotFound && (Options & (__FILE_CREATE | __FILE_CREATE_RECURSIVE))) {
                TRACE("File was not found, but options are to create 0x%x", Options);
                Code    = Filesystem->Module->CreatePath(&Filesystem->Descriptor, SubPath, Options, &Entry);
                Created = 1;
            }

            // Sanitize the open otherwise we must cleanup
            if (Code == FsOk) {
                // It's important here that we check if the flag
                // __FILE_FAILONEXIST has been set, then we return
                // the appropriate code instead of opening a new handle
                // Also this is ok if file was just created
                if ((Options & __FILE_FAILONEXIST) && Created == 0) {
                    ERROR("Entry already exists in path. FailOnExists has been specified.");
                    Code = Filesystem->Module->CloseEntry(&Filesystem->Descriptor, Entry);
                    Entry = NULL;
                }
                else {
                    Entry->System       = (uintptr_t*)Filesystem;
                    Entry->Path         = MStringCreate((void*)MStringRaw(Path), StrUTF8);
                    Entry->Hash         = MStringHash(Path);
                    Entry->IsLocked     = UUID_INVALID;
                    Entry->References   = 0;

                    // Take care of truncation flag if file was not newly created. The entry type
                    // must equal to file otherwise we will ignore the flag
                    if ((Options & __FILE_TRUNCATE) && Created == 0 && VfsEntryIsFile(Entry)) {
                        Code = Filesystem->Module->ChangeFileSize(&Filesystem->Descriptor, Entry, 0);
                    }
                    Key.Value = (int)Entry->Hash;
                    CollectionAppend(VfsGetOpenFiles(), CollectionCreateNode(Key, Entry));
                }
            }
            else {
                TRACE("File opening/creation failed with code: %i", Code);
                Entry = NULL;
            }
            MStringDestroy(SubPath);
        }

        // Now we can open the handle
        // Open Handle Internal takes care of these flags APPEND/VOLATILE/BINARY
        if (Entry != NULL) {
            Code = VfsOpenHandleInternal(Entry, Handle);
            if (Code == FsOk) {
                Entry->References++;
            }
        }
    }
    return Code;
}

/* VfsGuessBasePath
 * Tries to guess the base path of the relative file path in case
 * the working directory cannot be resolved. */
OsStatus_t
VfsGuessBasePath(
    _In_  const char*   Path,
    _Out_ char*         Result)
{
    char *dot = strrchr(Path, '.');

    TRACE("VfsGuessBasePath(%s)", Path);
    if (dot) {
        // Binaries are found in common
        if (!strcmp(dot, ".app") || !strcmp(dot, ".dll")) {
            memcpy(Result, "$bin/", 5);
        }
        // Resources are found in system folder
        else {
            memcpy(Result, "$sys/", 5);
        }
    }
    // Assume we are looking for folders in system folder
    else {
        memcpy(Result, "$sys/", 5);
    }
    TRACE("=> %s", Result);
    return OsSuccess;
}

/* VfsResolvePath
 * Resolves the undetermined abs or relative path. */
MString_t*
VfsResolvePath(
    _In_ UUId_t         Requester,
    _In_ const char*    Path)
{
    MString_t *PathResult = NULL;

    TRACE("VfsResolvePath(%s)", Path);
    if (strchr(Path, ':') == NULL && strchr(Path, '$') == NULL) {
        char *BasePath  = (char*)malloc(_MAXPATH);
        memset(BasePath, 0, _MAXPATH);
        if (GetWorkingDirectoryOfApplication(Requester, &BasePath[0], _MAXPATH) == OsError) {
            if (VfsGuessBasePath(Path, &BasePath[0]) == OsError) {
                ERROR("Failed to guess the base path for path %s", Path);
                return NULL;
            }
        }
        else {
            strcat(BasePath, "/");
        }
        strcat(BasePath, Path);
        PathResult = VfsPathCanonicalize(BasePath);
    }
    else {
        PathResult = VfsPathCanonicalize(Path);
    }
    return PathResult;
}

/* VfsOpenEntry
 * Opens or creates the given file path based on
 * the given <Access> and <Options> flags. See the top of this file */
FileSystemCode_t
VfsOpenEntry(
    _In_  UUId_t        Requester,
    _In_  const char*   Path, 
    _In_  Flags_t       Options, 
    _In_  Flags_t       Access,
    _Out_ UUId_t*       FileId)
{
    FileSystemEntryHandle_t* Handle = NULL;
    FileSystemCode_t Code = FsPathNotFound;
    MString_t* mPath;
    DataKey_t Key;

    TRACE("VfsOpenEntry(Path %s, Options 0x%x, Access 0x%x)", Path, Options, Access);
    if (Path == NULL) {
        return FsInvalidParameters;
    }

    // If path is not absolute or special, we 
    // must try the working directory of caller
    mPath = VfsResolvePath(Requester, Path);
    if (mPath != NULL) {
        Code = VfsOpenInternal(mPath, Options, Access, &Handle);
    }
    MStringDestroy(mPath);

    // Sanitize code
    if (Code != FsOk) {
        TRACE("Error opening entry, exited with code: %i", Code);
        *FileId = UUID_INVALID;
    }
    else {
        Handle->Id      = VfsIdentifierFileGet();
        Handle->Owner   = Requester;
        Handle->Access  = Access;
        Handle->Options = Options;
        
        Key.Value = (int)Handle->Id;
        CollectionAppend(VfsGetOpenHandles(), CollectionCreateNode(Key, Handle));
        *FileId = Handle->Id;
    }
    return Code;
}

/* VfsCloseEntry
 * Closes the given file-handle, but does not necessarily
 * close the link to the file. Returns the result */
FileSystemCode_t
VfsCloseEntry(
    _In_ UUId_t         Requester,
    _In_ UUId_t         Handle)
{
    FileSystemEntryHandle_t*    EntryHandle;
    FileSystemEntry_t*          Entry;
    FileSystemCode_t            Code;
    CollectionItem_t* Node;
    FileSystem_t *Fs;
    DataKey_t Key;

    TRACE("VfsCloseEntry(Handle %u)", Handle);

    Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
    if (Code != FsOk) {
        return Code;
    }
    Key.Value   = (int)Handle;
    Node        = CollectionGetNodeByKey(VfsGetOpenHandles(), Key, 0);
    Entry       = EntryHandle->Entry;

    // Handle file specific flags
    if (VfsEntryIsFile(EntryHandle->Entry)) {
        // If there has been allocated any buffers they should
        // be flushed and cleaned up 
        if (!(EntryHandle->Options & __FILE_VOLATILE)) {
            VfsFlushFile(Requester, Handle);
            free(EntryHandle->OutBuffer);
        }
    }

    // Call the filesystem close-handle to cleanup
    Fs      = (FileSystem_t*)EntryHandle->Entry->System;
    Code    = Fs->Module->CloseHandle(&Fs->Descriptor, EntryHandle);
    if (Code != FsOk) {
        return Code;
    }
    CollectionRemoveByNode(VfsGetOpenHandles(), Node);
    free(Node);

    // Take care of any entry cleanup / reduction
    Entry->References--;
    if (Entry->IsLocked == Requester) {
        Entry->IsLocked = UUID_INVALID;
    }

    // Last reference?
    // Cleanup the file in case of no refs
    if (Entry->References == 0) {
        Key.Value = (int)Entry->Hash;
        CollectionRemoveByKey(VfsGetOpenFiles(), Key);
        Code = Fs->Module->CloseEntry(&Fs->Descriptor, Entry);
    }
    return Code;
}

/* VfsDeletePath
 * Deletes the given path the caller must make sure there is no other references
 * to the file - otherwise delete fails */
FileSystemCode_t
VfsDeletePath(
    _In_ UUId_t         Requester, 
    _In_ const char*    Path,
    _In_ Flags_t        Options)
{
    FileSystemEntryHandle_t* EntryHandle;
    FileSystemCode_t Code;
    FileSystem_t *Fs;
    MString_t *SubPath = NULL;
    MString_t *mPath;
    UUId_t Handle;
    DataKey_t Key;

    TRACE("VfsDeletePath(Path %s, Options 0x%x)", Path, Options);
    if (Path == NULL) {
        return FsInvalidParameters;
    }

    // If path is not absolute or special, we should ONLY try either
    // the current working directory.
    mPath = VfsResolvePath(Requester, Path);
    if (mPath == NULL) {
        return FsPathNotFound;
    }
    Fs = VfsGetFileSystemFromPath(mPath, &SubPath);
    MStringDestroy(mPath);
    if (Fs == NULL) {
        return FsPathNotFound;
    }

    // First step is to open the path in exclusive mode
    Code = VfsOpenEntry(Requester, Path, __FILE_VOLATILE, __FILE_READ_ACCESS | __FILE_WRITE_ACCESS, &Handle);
    if (Code == FsOk) {
        Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
        if (Code != FsOk) {
            return Code;
        }
        Key.Value   = (int)EntryHandle->Entry->Hash;
        Code        = Fs->Module->DeleteEntry(&Fs->Descriptor, EntryHandle);
        if (Code == FsOk) {
            // Cleanup handles and open file
            CollectionRemoveByKey(VfsGetOpenFiles(), Key);
            Key.Value = (int)Handle;
            CollectionRemoveByKey(VfsGetOpenHandles(), Key);
        }
    }
    return Code;
}

/* VfsReadEntry
 * Reads the requested number of bytes into the given buffer
 * from the current position in the handle filehandle */
FileSystemCode_t
VfsReadEntry(
    _In_  UUId_t                    Requester,
    _In_  UUId_t                    Handle,
    _In_  UUId_t                    BufferHandle,
    _In_  size_t                    Length,
    _Out_ size_t*                   BytesIndex,
    _Out_ size_t*                   BytesRead)
{
    FileSystemEntryHandle_t* EntryHandle;
    FileSystemCode_t Code;
    FileSystem_t* Fs;
    DmaBuffer_t* Buffer;

    if (BufferHandle == UUID_INVALID || Length == 0) {
        ERROR("Buffer/length is invalid.");
        return FsInvalidParameters;
    }

    Code = VfsIsHandleValid(Requester, Handle, __FILE_READ_ACCESS, &EntryHandle);
    if (Code != FsOk) {
        return Code;
    }

    // Sanity -> Flush if we wrote and now read
    if ((EntryHandle->LastOperation != __FILE_OPERATION_READ) && VfsEntryIsFile(EntryHandle->Entry)) {
        Code = VfsFlushFile(Requester, Handle);
    }

    // Acquire the buffer for reading
    Buffer = CreateBuffer(BufferHandle, 0);
    if (Buffer == NULL) {
        ERROR("User specified buffer was invalid");
        return FsInvalidParameters;
    }

    Fs      = (FileSystem_t*)EntryHandle->Entry->System;
    Code    = Fs->Module->ReadEntry(&Fs->Descriptor, EntryHandle, Buffer, Length, BytesIndex, BytesRead);
    if (Code == FsOk) {
        EntryHandle->LastOperation  = __FILE_OPERATION_READ;
        EntryHandle->Position       += *BytesRead;
    }
    DestroyBuffer(Buffer);
    return Code;
}

/* VsfWriteEntry
 * Writes the requested number of bytes from the given buffer
 * into the current position in the filehandle */
FileSystemCode_t
VsfWriteEntry(
    _In_  UUId_t                    Requester,
    _In_  UUId_t                    Handle,
    _In_  UUId_t                    BufferHandle,
    _In_  size_t                    Length,
    _Out_ size_t*                   BytesWritten)
{
    FileSystemEntryHandle_t* EntryHandle;
    FileSystemCode_t Code;
    FileSystem_t* Fs;
    DmaBuffer_t* Buffer;

    TRACE("VsfWriteEntry(Length %u)", Length);

    if (BufferHandle == UUID_INVALID || Length == 0) {
        ERROR("Buffer/length is invalid.");
        return FsInvalidParameters;
    }

    Code = VfsIsHandleValid(Requester, Handle, __FILE_WRITE_ACCESS, &EntryHandle);
    if (Code != FsOk) {
        return Code;
    }

    // Sanity -> Clear read buffer if we are writing
    if ((EntryHandle->LastOperation != __FILE_OPERATION_WRITE) && VfsEntryIsFile(EntryHandle->Entry)) {
        Code = VfsFlushFile(Requester, Handle);
    }

    // Acquire the buffer for writing
    Buffer = CreateBuffer(BufferHandle, 0);
    if (Buffer == NULL) {
        ERROR("User specified buffer was invalid");
        return FsInvalidParameters;
    }

    Fs      = (FileSystem_t*)EntryHandle->Entry->System;
    Code    = Fs->Module->WriteEntry(&Fs->Descriptor, EntryHandle, Buffer, Length, BytesWritten);
    if (Code == FsOk) {
        EntryHandle->LastOperation  = __FILE_OPERATION_WRITE;
        EntryHandle->Position       += *BytesWritten;
        if (EntryHandle->Position > EntryHandle->Entry->Descriptor.Size.QuadPart) {
            EntryHandle->Entry->Descriptor.Size.QuadPart = EntryHandle->Position;
        }
    }
    DestroyBuffer(Buffer);
    return Code;
}

/* VfsSeekInEntry
 * Seeks in the given file entry handle. Seeks to the absolute position given. */
FileSystemCode_t
VfsSeekInEntry(
    _In_ UUId_t     Requester,
    _In_ UUId_t     Handle, 
    _In_ uint32_t   SeekLo, 
    _In_ uint32_t   SeekHi)
{
    FileSystemEntryHandle_t *EntryHandle = NULL;
    FileSystemCode_t Code;
    FileSystem_t *Fs;

    // Combine two u32 to form one big u64 
    // This is just the declaration
    union {
        struct {
            uint32_t Lo;
            uint32_t Hi;
        } Parts;
        uint64_t Full;
    } SeekAbs;
    SeekAbs.Parts.Lo = SeekLo;
    SeekAbs.Parts.Hi = SeekHi;

    TRACE("VfsSeekFile(Handle %u, SeekLo 0x%x, SeekHi 0x%x)", Handle, SeekLo, SeekHi);

    Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
    if (Code != FsOk) {
        return Code;
    }

    // Flush buffers before seeking
    if (!(EntryHandle->Options & __FILE_VOLATILE) && VfsEntryIsFile(EntryHandle->Entry)) {
        Code = VfsFlushFile(Requester, Handle);
    }

    // Perform the seek on a file-system level
    Fs      = (FileSystem_t*)EntryHandle->Entry->System;
    Code    = Fs->Module->SeekInEntry(&Fs->Descriptor, EntryHandle, SeekAbs.Full);
    if (Code == FsOk) {
        EntryHandle->LastOperation      = __FILE_OPERATION_NONE;
        EntryHandle->OutBufferPosition  = 0;
    }
    return Code;
}

/* VfsFlushFile
 * Flushes the internal file buffers and ensures there are
 * no pending file operations for the given file handle */
FileSystemCode_t
VfsFlushFile(
    _In_ UUId_t Requester, 
    _In_ UUId_t Handle)
{
    FileSystemEntryHandle_t *EntryHandle = NULL;
    FileSystemCode_t Code;
    //FileSystem_t *Fs;

    Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
    if (Code != FsOk) {
        return Code;
    }

    // If no buffering enabled skip, or if not a file skip
    if ((EntryHandle->Options & __FILE_VOLATILE) || !VfsEntryIsFile(EntryHandle->Entry)) {
        return FsOk;
    }

    // Empty output buffer 
    // - But sanitize the buffers first
    if (EntryHandle->OutBuffer != NULL && EntryHandle->OutBufferPosition != 0) {
        size_t BytesWritten = 0;
#if 0
        Fs      = (FileSystem_t*)EntryHandle->File->System;
        Code    = Fs->Module->WriteFile(&Fs->Descriptor, EntryHandle, NULL, &BytesWritten);
#endif
        if (BytesWritten != EntryHandle->OutBufferPosition) {
            return FsDiskError;
        }
    }
    return Code;
}

/* VfsMoveEntry
 * Moves or copies a given file path to the destination path
 * this can also be used for renamining if the dest/source paths
 * match (except for filename/directoryname) */
FileSystemCode_t
VfsMoveEntry(
    _In_ UUId_t         Requester,
    _In_ const char*    Source, 
    _In_ const char*    Destination,
    _In_ int            Copy)
{
    // @todo implement using existing fs functions
    _CRT_UNUSED(Requester);
    _CRT_UNUSED(Source);
    _CRT_UNUSED(Destination);
    _CRT_UNUSED(Copy);
    return FsOk;
}

/* VfsGetEntryPosition 
 * Queries the current entry position that the given handle
 * is at, it returns as two seperate unsigned values, the upper
 * value is optional and should only be checked for large entries */
OsStatus_t
VfsGetEntryPosition(
    _In_  UUId_t                    Requester,
    _In_  UUId_t                    Handle,
    _Out_ QueryFileValuePackage_t*  Result)
{
    FileSystemEntryHandle_t *EntryHandle = NULL;
    FileSystemCode_t Code;

    Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
    if (Code != FsOk) {
        return OsError;
    }

    Result->Value.Full  = EntryHandle->Position;
    Result->Code        = Code;
    return OsSuccess;
}

/* VfsGetEntryOptions
 * Queries the current entry options and entry access flags for the given file handle */
OsStatus_t
VfsGetEntryOptions(
    _In_  UUId_t                        Requester,
    _In_  UUId_t                        Handle,
    _Out_ QueryFileOptionsPackage_t*    Result)
{
    FileSystemEntryHandle_t *EntryHandle = NULL;
    FileSystemCode_t Code;

    Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
    if (Code != FsOk) {
        return OsError;
    }

    Result->Options = EntryHandle->Options;
    Result->Access  = EntryHandle->Access;
    Result->Code    = Code;
    return OsSuccess;
}

/* VfsSetEntryOptions 
 * Attempts to modify the current option and or access flags
 * for the given entry handle as specified by <Options> and <Access> */
OsStatus_t
VfsSetEntryOptions(
    _In_ UUId_t     Requester,
    _In_ UUId_t     Handle,
    _In_ Flags_t    Options,
    _In_ Flags_t    Access)
{
    FileSystemEntryHandle_t *EntryHandle = NULL;
    FileSystemCode_t Code;

    Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
    if (Code != FsOk) {
        return OsError;
    }

    EntryHandle->Options    = Options;
    EntryHandle->Access     = Access;
    return OsSuccess;
}

/* VfsGetEntrySize 
 * Queries the current file-entry size that the given handle
 * has, it returns as two seperate unsigned values, the upper
 * value is optional and should only be checked for large entries */
OsStatus_t
VfsGetEntrySize(
    _In_  UUId_t                    Requester,
    _In_  UUId_t                    Handle,
    _Out_ QueryFileValuePackage_t*  Result)
{
    FileSystemEntryHandle_t *EntryHandle = NULL;
    FileSystemCode_t Code;

    Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
    if (Code != FsOk) {
        return OsError;
    }
    
    Result->Value.Full  = EntryHandle->Entry->Descriptor.Size.QuadPart;
    Result->Code        = Code;
    return OsSuccess;
}

/* VfsGetEntryPath 
 * Queries the full path of a file-entry that the given handle
 * has, it returns it as a UTF8 string with max length of _MAXPATH */
OsStatus_t
VfsGetEntryPath(
    _In_  UUId_t        Requester,
    _In_  UUId_t        Handle,
    _Out_ MString_t**   Path)
{
    FileSystemEntryHandle_t *EntryHandle = NULL;
    FileSystemCode_t Code;

    Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
    if (Code != FsOk) {
        return OsError;
    }
    
    *Path = EntryHandle->Entry->Path;
    return OsSuccess;
}

/* VfsQueryEntryPath
 * Queries information about the filesystem entry through its full path. */
FileSystemCode_t
VfsQueryEntryPath(
    _In_ UUId_t                     Requester,
    _In_ const char*                Path,
    _In_ OsFileDescriptor_t*        Information)
{
    FileSystemCode_t Code;
    UUId_t Handle;

    Code = VfsOpenEntry(Requester, Path, 0, __FILE_READ_ACCESS | __FILE_READ_SHARE, &Handle);
    if (Code == FsOk) {
        Code = VfsQueryEntryHandle(Requester, Handle, Information);
        // No reason to check on the code, the assumption is it always go ok
        Code = VfsCloseEntry(Requester, Handle);
    }
    return Code;
}

/* VfsQueryEntryHandle
 * Queries informatino about the filesystem entry through its handle. */
FileSystemCode_t
VfsQueryEntryHandle(
    _In_ UUId_t                     Requester,
    _In_ UUId_t                     Handle,
    _In_ OsFileDescriptor_t*        Information)
{
    FileSystemEntryHandle_t *EntryHandle = NULL;
    FileSystemCode_t Code;

    Code = VfsIsHandleValid(Requester, Handle, 0, &EntryHandle);
    if (Code == FsOk) {
        memcpy((void*)Information, (const void*)&EntryHandle->Entry->Descriptor, sizeof(OsFileDescriptor_t));
    }
    return Code;
}
