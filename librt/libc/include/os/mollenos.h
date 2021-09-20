/**
 * MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Definitions & Structures
 * - This header describes the os-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __MOLLENOS_H__
#define __MOLLENOS_H__

#include <os/osdefs.h>
#include <os/types/file.h>
#include <os/types/storage.h>
#include <os/types/path.h>
#include <os/types/thread.h>
#include <os/types/memory.h>
#include <time.h>

PACKED_TYPESTRUCT(SystemDescriptor, {
    size_t NumberOfProcessors;
    size_t NumberOfActiveCores;

    size_t PagesTotal;
    size_t PagesUsed;
    size_t PageSizeBytes;
    size_t AllocationGranularityBytes;
});

PACKED_TYPESTRUCT(SystemTime, {
    LargeUInteger_t Nanoseconds;
    int             Second;
    int             Minute;
    int             Hour;
    int             DayOfMonth;
    int             Month;
    int             Year;
});

/* Cache Type Definitions
 * Flags that can be used when requesting a flush of one of the hardware caches */
#define CACHE_INSTRUCTION   1
#define CACHE_MEMORY        2

_CODE_BEGIN
/*******************************************************************************
 * Memory Extensions
 *******************************************************************************/
CRTDECL(OsStatus_t, MemoryAllocate(void* Hint, size_t Length, unsigned int Flags, void** MemoryOut));
CRTDECL(OsStatus_t, MemoryFree(void* Memory, size_t Length));
CRTDECL(OsStatus_t, MemoryProtect(void* Memory, size_t Length, unsigned int Flags, unsigned int* PreviousFlags));
CRTDECL(OsStatus_t, MemoryQueryAllocation(void* Memory, MemoryDescriptor_t* DescriptorOut));
CRTDECL(OsStatus_t, MemoryQueryAttributes(void* Memory, size_t Length, unsigned int* AttributeArray));

/*******************************************************************************
 * System Extensions
 *******************************************************************************/
CRTDECL(int,        OsStatusToErrno(OsStatus_t Status));
CRTDECL(OsStatus_t, SystemQuery(SystemDescriptor_t* Descriptor));
CRTDECL(OsStatus_t, GetSystemTime(SystemTime_t* Time));
CRTDECL(OsStatus_t, GetSystemTick(int TickBase, LargeUInteger_t* Tick));
CRTDECL(OsStatus_t, QueryPerformanceFrequency(LargeInteger_t* Frequency));
CRTDECL(OsStatus_t, QueryPerformanceTimer(LargeInteger_t* Value));
CRTDECL(OsStatus_t, FlushHardwareCache(int Cache, void* Start, size_t Length));

/*******************************************************************************
 * Threading Extensions
 *******************************************************************************/
CRTDECL(void,       InitializeThreadParameters(ThreadParameters_t* Paramaters));
CRTDECL(OsStatus_t, SetCurrentThreadName(const char *ThreadName));
CRTDECL(OsStatus_t, GetCurrentThreadName(char *ThreadNameBuffer, size_t MaxLength));

/*******************************************************************************
 * Path Extensions
 *******************************************************************************/
CRTDECL(OsStatus_t, PathCanonicalize(const char* Path, char* Buffer, size_t MaxLength));
CRTDECL(OsStatus_t, PathResolveEnvironment(EnvironmentPath_t Base, char* Buffer, size_t MaxLength));
CRTDECL(OsStatus_t, SetWorkingDirectory(const char *Path));
CRTDECL(OsStatus_t, GetWorkingDirectory(char* PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetAssemblyDirectory(char *PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetUserDirectory(char *PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetUserCacheDirectory(char *PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetApplicationDirectory(char *PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetApplicationTemporaryDirectory(char *PathBuffer, size_t MaxLength));

/*******************************************************************************
 * File Extensions
 *******************************************************************************/
// CreateFileMapping::Flags
#define FILE_MAPPING_READ       0x00000001U
#define FILE_MAPPING_WRITE      0x00000002U
#define FILE_MAPPING_EXECUTE    0x00000004U

CRTDECL(OsStatus_t, SetFileSizeFromPath(const char* path, size_t size));
CRTDECL(OsStatus_t, SetFileSizeFromFd(int fileDescriptor, size_t size));
CRTDECL(OsStatus_t, ChangeFilePermissionsFromPath(const char* path, unsigned int permissions));
CRTDECL(OsStatus_t, ChangeFilePermissionsFromFd(int fileDescriptor, unsigned int permissions));
CRTDECL(OsStatus_t, GetFileLink(const char* path, char* linkPathBuffer, size_t bufferLength));
CRTDECL(OsStatus_t, GetFilePathFromFd(int fileDescriptor, char *pathBuffer, size_t maxLength));
CRTDECL(OsStatus_t, GetStorageInformationFromPath(const char *path, OsStorageDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetStorageInformationFromFd(int fileDescriptor, OsStorageDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetFileSystemInformationFromPath(const char *path, OsFileSystemDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetFileSystemInformationFromFd(int fileDescriptor, OsFileSystemDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetFileInformationFromPath(const char *path, OsFileDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetFileInformationFromFd(int fileDescriptor, OsFileDescriptor_t* descriptor));
CRTDECL(OsStatus_t, CreateFileMapping(int fileDescriptor, int flags, uint64_t offset, size_t length, void** mapping));
CRTDECL(OsStatus_t, FlushFileMapping(void* mapping, size_t length));
CRTDECL(OsStatus_t, DestroyFileMapping(void* mapping));

_CODE_END
#endif //!__MOLLENOS_H__
