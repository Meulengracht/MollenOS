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
 * @brief
 * @param path
 * @param flags
 * @param permissions
 * @param handleOut
 * @return
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
 * @brief
 * @param path
 * @param permissions
 * @return
 */
CRTDECL(oserr_t,
OSMakeDirectory(
        _In_ const char*  path,
        _In_ unsigned int permissions));

/**
 * @brief
 * @param handle
 * @param entry
 * @return
 */
CRTDECL(oserr_t,
OSReadDirectory(
        _In_ uuid_t              handle,
        _In_ OSDirectoryEntry_t* entry));

/**
 * @brief
 * @param handle
 * @param position
 * @return
 */
CRTDECL(oserr_t,
OSSeekFile(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* position));

/**
 * @brief
 * @param handle
 * @param position
 * @return
 */
CRTDECL(oserr_t,
OSGetFilePosition(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* position));

/**
 * @brief
 * @param handle
 * @param size
 * @return
 */
CRTDECL(oserr_t,
OSGetFileSize(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* size));

/**
 * @brief
 * @param handle
 * @param size
 * @return
 */
CRTDECL(oserr_t,
OSSetFileSize(
        _In_ uuid_t        handle,
        _In_ UInteger64_t* size));

/**
 * @brief Moves, or optionally copies a file.
 * @param[In] from The source file that should be copied or moved.
 * @param[In] to The destination of the file
 * @param[In] copy If set, copy the file instead of moving it
 * @return OS_EOK if the operation succeeded, otherwise the an error code.
 */
CRTDECL(oserr_t,
OSMoveFile(
        _In_ const char* from,
        _In_ const char* to,
        _In_ bool        copy));

/**
 * @brief Creates either a hard link or symbolic link to a path
 * @param[In] from The source path that should be linked.
 * @param[In] to The path of the link
 * @param[In] symbolic If set, creates a symbolic link
 * @return OS_EOK if the operation succeeded, otherwise the an error code.
 */
CRTDECL(oserr_t,
OSLinkPath(
        _In_ const char* from,
        _In_ const char* to,
        _In_ bool        symbolic));

/**
 * brief Reads or writes a number of bytes to a file.
 * @param handle
 * @param bufferID
 * @param bufferOffset
 * @param write
 * @param length
 * @param bytesTransferred
 * @return
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
 * @brief
 * @param path
 * @param size
 * @return
 */
CRTDECL(oserr_t,
SetFileSizeFromPath(
        _In_ const char* path,
        _In_ size_t      size));

/**
 * @brief
 * @param fileDescriptor
 * @param size
 * @return
 */
CRTDECL(oserr_t,
SetFileSizeFromFd(
        _In_ int    fileDescriptor,
        _In_ size_t size));

/**
 * @brief
 * @param path
 * @param permissions
 * @return
 */
CRTDECL(oserr_t,
ChangeFilePermissionsFromPath(
        _In_ const char*  path,
        _In_ unsigned int permissions));

/**
 * @brief
 * @param fileDescriptor
 * @param permissions
 * @return
 */
CRTDECL(oserr_t,
ChangeFilePermissionsFromFd(
        _In_ int          fileDescriptor,
        _In_ unsigned int permissions));

/**
 * @brief
 * @param fileDescriptor
 * @param access
 * @return
 */
CRTDECL(oserr_t,
ChangeFileHandleAccessFromFd(
        _In_ int          fileDescriptor,
        _In_ unsigned int access));
/**
 * @brief
 * @param path
 * @param linkPathBuffer
 * @param bufferLength
 * @return
 */
CRTDECL(oserr_t,
GetFileLink(
        _In_ const char* path,
        _In_ char*       linkPathBuffer,
        _In_ size_t      bufferLength));

/**
 * @brief
 * @param fileDescriptor
 * @param buffer
 * @param maxLength
 * @return
 */
CRTDECL(oserr_t,
GetFilePathFromFd(
        _In_ int    fileDescriptor,
        _In_ char*  buffer,
        _In_ size_t maxLength));

/**
 * @brief
 * @param path
 * @param followLinks
 * @param descriptor
 * @return
 */
CRTDECL(oserr_t,
GetStorageInformationFromPath(
        _In_ const char*            path,
        _In_ int                    followLinks,
        _In_ OSStorageDescriptor_t* descriptor));

/**
 * @brief
 * @param fileDescriptor
 * @param descriptor
 * @return
 */
CRTDECL(oserr_t,
GetStorageInformationFromFd(
        _In_ int                    fileDescriptor,
        _In_ OSStorageDescriptor_t* descriptor));

/**
 * @brief
 * @param path
 * @param followLinks
 * @param descriptor
 * @return
 */
CRTDECL(oserr_t,
GetFileSystemInformationFromPath(
        _In_ const char*               path,
        _In_ int                       followLinks,
        _In_ OSFileSystemDescriptor_t* descriptor));

/**
 * @brief
 * @param fileDescriptor
 * @param descriptor
 * @return
 */
CRTDECL(oserr_t,
GetFileSystemInformationFromFd(
        _In_ int                       fileDescriptor,
        _In_ OSFileSystemDescriptor_t* descriptor));

/**
 * @brief
 * @param path
 * @param followLinks
 * @param descriptor
 * @return
 */
CRTDECL(oserr_t,
GetFileInformationFromPath(
        const char*         path,
        int                 followLinks,
        OSFileDescriptor_t* descriptor));

/**
 * @brief
 * @param fileDescriptor
 * @param descriptor
 * @return
 */
CRTDECL(oserr_t,
GetFileInformationFromFd(
        _In_ int                 fileDescriptor,
        _In_ OSFileDescriptor_t* descriptor));

/**
 * Available flags for creating fileviews
 */
#define FILEVIEW_READ        0x00000001U
#define FILEVIEW_WRITE       0x00000002U
#define FILEVIEW_EXECUTE     0x00000004U
#define FILEVIEW_SHARED      0x00000008U
#define FILEVIEW_COPYONWRITE 0x00000010U
#define FILEVIEW_32BIT       0x00000020U
#define FILEVIEW_POPULATE    0x00000040U

/**
 * Available flags for flushing fileviews
 */
#define FILEVIEW_FLUSH_ASYNC      0x1
#define FILEVIEW_FLUSH_INVALIDATE 0x2

/**
 * @brief
 * @param fileDescriptor
 * @param flags
 * @param offset
 * @param length
 * @param mapping
 * @return
 */
CRTDECL(oserr_t,
OSFileViewCreate(
        _In_  OSHandle_t*  handle,
        _In_  unsigned int flags,
        _In_  uint64_t     offset,
        _In_  size_t       length,
        _Out_ void**       mappingOut));

/**
 * @brief
 * @param mapping
 * @param length
 * @return
 */
CRTDECL(oserr_t,
OSFileViewFlush(
        _In_ void*        mapping,
        _In_ size_t       length,
        _In_ unsigned int flags));

/**
 * @brief
 * @param mapping
 * @return
 */
CRTDECL(oserr_t,
OSFileViewUnmap(
        _In_ void*  mapping,
        _In_ size_t length));

_CODE_END
#endif //!__OS_SERVICES_FILE_H__
