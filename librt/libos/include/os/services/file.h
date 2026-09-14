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
 */

#ifndef __OS_SERVICES_FILE_H__
#define __OS_SERVICES_FILE_H__

#include <os/types/file.h>
#include <os/types/storage.h>
#include <os/types/handle.h>

_CODE_BEGIN

/**
 * @brief Opens a file or directory at the specified path.
 * @param path Path to open.
 * @param flags Open and creation flags.
 * @param permissions Permissions to apply when creating the path.
 * @param handleOut Receives the opened handle.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSOpenPath(
        _In_  const char*  path,
        _In_  unsigned int flags,
        _In_  unsigned int permissions,
        _Out_ OSHandle_t*  handleOut));

/**
 * @brief Unlinks/removes a path.
 * @param path The path to unlink.
 * @return The status of the operation.
 */
CRTDECL(oserr_t,
OSUnlinkPath(
        _In_ const char* path));

/**
 * @brief Creates a directory at the specified path.
 * @param path Directory path to create.
 * @param permissions Permissions to apply to the new directory.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSMakeDirectory(
        _In_ const char*  path,
        _In_ unsigned int permissions));

/**
 * @brief Reads the next directory entry from an open directory.
 * @param handle Handle of the directory to read.
 * @param entry Receives the directory entry.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSReadDirectory(
        _In_ uuid_t              handle,
        _In_ OSDirectoryEntry_t* entry));

/**
 * @brief Changes the current position of an open file.
 * @param handle Handle of the file to seek.
 * @param position New file position, in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSeekFile(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* position));

/**
 * @brief Retrieves the current position of an open file.
 * @param handle Handle of the file to query.
 * @param position Receives the current file position, in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSGetFilePosition(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* position));

/**
 * @brief Retrieves the size of an open file.
 * @param handle Handle of the file to query.
 * @param size Receives the file size, in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSGetFileSize(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* size));

/**
 * @brief Changes the size of an open file.
 * @param handle Handle of the file to resize.
 * @param size New file size, in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSSetFileSize(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* size));

/**
 * @brief Moves, or optionally copies a file.
 * @param from The source file that should be copied or moved.
 * @param to The destination of the file.
 * @param copy If set, copy the file instead of moving it.
 * @return OS_EOK if the operation succeeded, otherwise the an error code.
 */
CRTDECL(oserr_t,
OSMoveFile(
        _In_ const char* from,
        _In_ const char* to,
        _In_ bool        copy));

/**
 * @brief Creates either a hard link or symbolic link to a path
 * @param from The source path that should be linked.
 * @param to The path of the link.
 * @param symbolic If set, creates a symbolic link.
 * @return OS_EOK if the operation succeeded, otherwise the an error code.
 */
CRTDECL(oserr_t,
OSLinkPath(
        _In_ const char* from,
        _In_ const char* to,
        _In_ bool        symbolic));

/**
 * @brief Reads or writes a number of bytes to a file.
 * @param handle File handle used for the transfer.
 * @param bufferID Identifier of the shared buffer used for the transfer.
 * @param bufferOffset Offset into the shared buffer.
 * @param write True to write to the file; false to read from it.
 * @param length Number of bytes to transfer.
 * @param bytesTransferred Receives the number of bytes transferred.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSTransferFile(
        _In_  uuid_t  handle,
        _In_  uuid_t  bufferID,
        _In_  size_t  bufferOffset,
        _In_  bool    write,
        _In_  size_t  length,
        _Out_ size_t* bytesTransferred));

/**
 * @brief Changes the size of a file identified by its path.
 * @param path Path of the file to resize.
 * @param size New file size, in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
SetFileSizeFromPath(
        _In_ const char* path,
        _In_ size_t      size));

/**
 * @brief Changes the size of a file identified by a file descriptor.
 * @param fileDescriptor File descriptor to resize.
 * @param size New file size, in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
SetFileSizeFromFd(
        _In_ int    fileDescriptor,
        _In_ size_t size));

/**
 * @brief Changes permissions for a file identified by its path.
 * @param path Path of the file to modify.
 * @param permissions New file permissions.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
ChangeFilePermissionsFromPath(
        _In_ const char*  path,
        _In_ unsigned int permissions));

/**
 * @brief Changes permissions for a file identified by a file descriptor.
 * @param fileDescriptor File descriptor to modify.
 * @param permissions New file permissions.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
ChangeFilePermissionsFromFd(
        _In_ int          fileDescriptor,
        _In_ unsigned int permissions));

/**
 * @brief Changes access flags for a file descriptor.
 * @param fileDescriptor File descriptor to modify.
 * @param access New access flags.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
ChangeFileHandleAccessFromFd(
        _In_ int          fileDescriptor,
        _In_ unsigned int access));
/**
 * @brief Reads the target path of a symbolic link.
 * @param path Path of the link to read.
 * @param linkPathBuffer Receives the link target path.
 * @param bufferLength Size of linkPathBuffer in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
GetFileLink(
        _In_ const char* path,
        _In_ char*       linkPathBuffer,
        _In_ size_t      bufferLength));

/**
 * @brief Retrieves the path associated with a file descriptor.
 * @param fileDescriptor File descriptor to query.
 * @param buffer Receives the path.
 * @param maxLength Size of buffer in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
GetFilePathFromFd(
        _In_ int    fileDescriptor,
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief Retrieves storage information for a path.
 * @param path Path whose storage should be queried.
 * @param followLinks Whether symbolic links should be followed.
 * @param descriptor Receives the storage descriptor.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
GetStorageInformationFromPath(
        _In_ const char*            path,
        _In_ int                    followLinks,
        _In_ OSStorageDescriptor_t* descriptor));

/**
 * @brief Retrieves storage information for a file descriptor.
 * @param fileDescriptor File descriptor to query.
 * @param descriptor Receives the storage descriptor.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
GetStorageInformationFromFd(
        _In_ int                    fileDescriptor,
        _In_ OSStorageDescriptor_t* descriptor));

/**
 * @brief Retrieves file-system information for a path.
 * @param path Path whose file system should be queried.
 * @param followLinks Whether symbolic links should be followed.
 * @param descriptor Receives the file-system descriptor.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
GetFileSystemInformationFromPath(
        _In_ const char*               path,
        _In_ int                       followLinks,
        _In_ OSFileSystemDescriptor_t* descriptor));

/**
 * @brief Retrieves file-system information for a file descriptor.
 * @param fileDescriptor File descriptor to query.
 * @param descriptor Receives the file-system descriptor.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
GetFileSystemInformationFromFd(
        _In_ int                       fileDescriptor,
        _In_ OSFileSystemDescriptor_t* descriptor));

/**
 * @brief Retrieves file information for a path.
 * @param path Path whose file should be queried.
 * @param followLinks Whether symbolic links should be followed.
 * @param descriptor Receives the file descriptor information.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
GetFileInformationFromPath(
        const char*         path,
        int                 followLinks,
        OSFileDescriptor_t* descriptor));

/**
 * @brief Retrieves file information for a file descriptor.
 * @param fileDescriptor File descriptor to query.
 * @param descriptor Receives the file descriptor information.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
GetFileInformationFromFd(
        _In_ int                 fileDescriptor,
        _In_ OSFileDescriptor_t* descriptor));

// TODO: OSFileLock
// TODO: OSFileUnlock

/**
 * Available flags for creating fileviews
 */
#define FILEVIEW_READ         0x00000001U
#define FILEVIEW_WRITE        0x00000002U
#define FILEVIEW_EXECUTE      0x00000004U
#define FILEVIEW_SHARED       0x00000008U
#define FILEVIEW_COPYONWRITE  0x00000010U
#define FILEVIEW_32BIT        0x00000020U
#define FILEVIEW_POPULATE     0x00000040U
#define FILEVIEW_BIGPAGES     0x00000080U
#define FILEVIEW_BIGPAGES_2MB (FILEVIEW_BIGPAGES | 0x00000100U)
#define FILEVIEW_BIGPAGES_1GB (FILEVIEW_BIGPAGES | 0x00000200U)

/**
 * Available flags for flushing fileviews
 */
#define FILEVIEW_FLUSH_ASYNC      0x1
#define FILEVIEW_FLUSH_INVALIDATE 0x2

/**
 * @brief Creates a memory mapping for an open file.
 * @param handle File handle to map.
 * @param flags File-view access and sharing flags.
 * @param offset File offset at which the mapping starts.
 * @param length Length of the mapping in bytes.
 * @param mappingOut Receives the mapped address.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSFileViewCreate(
        _In_  OSHandle_t*  handle,
        _In_  unsigned int flags,
        _In_  uint64_t     offset,
        _In_  size_t       length,
        _Out_ void**       mappingOut));

/**
 * @brief Flushes changes made through a file mapping.
 * @param mapping Address of the mapping to flush.
 * @param length Number of bytes to flush.
 * @param flags File-view flush behavior flags.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSFileViewFlush(
        _In_ void*        mapping,
        _In_ size_t       length,
        _In_ unsigned int flags));

/**
 * @brief Unmaps a file mapping.
 * @param mapping Address of the mapping to remove.
 * @param length Length of the mapping in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSFileViewUnmap(
        _In_ void*  mapping,
        _In_ size_t length));

_CODE_END
#endif //!__OS_SERVICES_FILE_H__
