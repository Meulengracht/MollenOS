/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * File Service Definitions & Structures
 * - This header describes the base file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __SERVICES_FILE_H__
#define __SERVICES_FILE_H__

#include <os/types/file.h>

// Access flags
#define __FILE_READ_ACCESS                      0x00000001
#define __FILE_WRITE_ACCESS                     0x00000002
#define __FILE_READ_SHARE                       0x00000100
#define __FILE_WRITE_SHARE                      0x00000200

// Options flag
#define __FILE_CREATE                           0x00000001
#define __FILE_CREATE_RECURSIVE                 0x00000002
#define __FILE_TRUNCATE                         0x00000004
#define __FILE_FAILONEXIST                      0x00000008
#define __FILE_APPEND                           0x00000100
#define __FILE_BINARY                           0x00000200
#define __FILE_VOLATILE                         0x00000400
#define __FILE_TEMPORARY                        0x00000800
#define __FILE_DIRECTORY                        0x00001000
#define __FILE_LINK                             0x00002000

/* OpenFile
 * Opens or creates the given file path based on
 * the given <Access> and <Options> flags. See the top of this file */
CRTDECL(FileSystemCode_t,
OpenFile(
    _In_  const char* Path, 
    _In_  Flags_t     Options, 
    _In_  Flags_t     Access,
    _Out_ UUId_t*     Handle));

/* CloseFile
 * Closes the given file-handle, but does not necessarily
 * close the link to the file. Returns the result */
CRTDECL(FileSystemCode_t,
CloseFile(
    _In_ UUId_t Handle));

/* DeletePath
 * Deletes the given path. The caller must make sure there is no other references
 * to the file or directory - otherwise delete fails */
CRTDECL(FileSystemCode_t,
DeletePath(
    _In_ const char* Path,
    _In_ Flags_t     Flags));

/* ReadFile
 * Reads the requested number of bytes into the given buffer
 * from the current position in the file-handle */
CRTDECL(FileSystemCode_t,
ReadFile(
    _In_      UUId_t  Handle,
    _In_      UUId_t  BufferHandle,
    _In_      size_t  Length,
    _Out_Opt_ size_t* BytesIndex,
    _Out_Opt_ size_t* BytesRead));

/* WriteFile
 * Writes the requested number of bytes from the given buffer
 * into the current position in the file-handle */
CRTDECL(FileSystemCode_t,
WriteFile(
    _In_      UUId_t  Handle,
    _In_      UUId_t  BufferHandle,
    _In_      size_t  Length,
    _Out_Opt_ size_t* BytesWritten));

/* SeekFile
 * Sets the file-pointer for the given handle to the
 * values given, the position is absolute and must
 * be within range of the file size */
CRTDECL(FileSystemCode_t,
SeekFile(
    _In_ UUId_t   Handle, 
    _In_ uint32_t SeekLo, 
    _In_ uint32_t SeekHi));

/* FlushFile
 * Flushes the internal file buffers and ensures there are
 * no pending file operations for the given file handle */
CRTDECL(FileSystemCode_t,
FlushFile(
    _In_ UUId_t Handle));

/* MoveFile
 * Moves or copies a given file path to the destination path
 * this can also be used for renamining if the dest/source paths
 * match (except for filename/directoryname) */
CRTDECL(FileSystemCode_t,
MoveFile(
    _In_ const char* Source,
    _In_ const char* Destination,
    _In_ int         Copy));

/* GetFilePosition 
 * Queries the current file position that the given handle
 * is at, it returns as two separate unsigned values, the upper
 * value is optional and should only be checked for large files */
CRTDECL(OsStatus_t,
GetFilePosition(
    _In_      UUId_t    Handle,
    _Out_     uint32_t* PositionLo,
    _Out_Opt_ uint32_t* PositionHi));

/* GetFileOptions 
 * Queries the current file options and file access flags
 * for the given file handle */
CRTDECL(OsStatus_t,
GetFileOptions(
    _In_  UUId_t   Handle,
    _Out_ Flags_t* Options,
    _Out_ Flags_t* Access));

/* SetFileOptions 
 * Attempts to modify the current option and or access flags
 * for the given file handle as specified by <Options> and <Access> */
CRTDECL(OsStatus_t,
SetFileOptions(
    _In_ UUId_t  Handle,
    _In_ Flags_t Options,
    _In_ Flags_t Access));

/* GetFileSize 
 * Queries the current file size that the given handle
 * has, it returns as two separate unsigned values, the upper
 * value is optional and should only be checked for large files */
CRTDECL(OsStatus_t,
GetFileSize(
    _In_      UUId_t    Handle,
    _Out_     uint32_t* SizeLo,
    _Out_Opt_ uint32_t* SizeHi));

/* GetFilePath 
 * Queries the full path of a file that the given handle
 * has, it returns it as a UTF8 string with max length of _MAXPATH */
CRTDECL(OsStatus_t,
GetFilePath(
    _In_  UUId_t Handle,
    _In_ char*   Path,
    _In_ size_t  MaxLength));

/* GetFileSystemStatsByPath 
 * Queries file information by the given path. If the path is invalid
 * or doesn't exist the information is zeroed out. */
CRTDECL(FileSystemCode_t,
GetFileSystemStatsByPath(
    _In_ const char*               Path,
    _In_ OsFileSystemDescriptor_t* FileDescriptor));

/* GetFileSystemStatsByHandle 
 * Queries file information by the given file handle. If the handle is invalid
 * or doesn't exist the information is zeroed out. */
CRTDECL(FileSystemCode_t,
GetFileSystemStatsByHandle(
    _In_ UUId_t                    Handle,
    _In_ OsFileSystemDescriptor_t* FileDescriptor));

/* GetFileStatsByPath 
 * Queries file information by the given path. If the path is invalid
 * or doesn't exist the information is zeroed out. */
CRTDECL(FileSystemCode_t,
GetFileStatsByPath(
    _In_ const char*         Path,
    _In_ OsFileDescriptor_t* FileDescriptor));

/* GetFileStatsByHandle 
 * Queries file information by the given file handle. If the handle is invalid
 * or doesn't exist the information is zeroed out. */
CRTDECL(FileSystemCode_t,
GetFileStatsByHandle(
    _In_ UUId_t              Handle,
    _In_ OsFileDescriptor_t* FileDescriptor));

#endif //!__SERVICES_FILE_H__
