/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * OS Interface
 * - This header describes the os-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __MOLLENOS_H__
#define __MOLLENOS_H__

#include <os/osdefs.h>
#include <os/types/file.h>
#include <os/types/memory.h>
#include <os/types/path.h>
#include <os/types/storage.h>
#include <os/types/thread.h>
#include <os/types/time.h>

typedef struct SystemDescriptor {
    size_t NumberOfProcessors;
    size_t NumberOfActiveCores;

    size_t PagesTotal;
    size_t PagesUsed;
    size_t PageSizeBytes;
    size_t AllocationGranularityBytes;
} SystemDescriptor_t;

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
CRTDECL(int,        OsStatusToErrno(OsStatus_t osStatus));
CRTDECL(OsStatus_t, SystemQuery(SystemDescriptor_t* descriptor));
CRTDECL(OsStatus_t, FlushHardwareCache(int Cache, void* Start, size_t Length));

/*******************************************************************************
 * Time Interface
 *******************************************************************************/

/**
 * @brief Puts the calling thread to sleep for the requested duration. The actual time
 * slept is not guaranteed, but will be returned in the remaining value.
 *
 * @param[In]            duration  The duration to sleep in nanoseconds.
 * @param[Out, Optional] remaining The remaining time if less time was slept than the value in timeout.
 * @return OsOK if the sleep was not interrupted. Otherwise returns OsInterrupted.
 */
CRTDECL(OsStatus_t,
VaSleep(
        _In_      LargeUInteger_t* duration,
        _Out_Opt_ LargeUInteger_t* remaining));

/**
 * @brief Stalls the current thread for the given duration. It will stall for atleast the duration
 * provided, but can be stalled for longer if the thread is scheduled.
 *
 * @param[In] duration The duration to stall the thread for in nanoseconds.
 * @return Will always succeed.
 */
CRTDECL(OsStatus_t,
VaStall(
        _In_ LargeUInteger_t* duration));

/**
 * @brief Reads the current wall clock from the kernel wallclock driver. No guarantee is made to
 * the precision of this time other than second-precision. However the value is in microseconds. The time
 * is represented in microseconds since Jaunary 1, 2020 UTC.
 *
 * @param[Out] time Pointer to where the time will be stored.
 * @return     Returns OsOK if the clock was read, otherwise OsNotSupported.
 */
CRTDECL(OsStatus_t,
VaGetWallClock(
        _In_ LargeInteger_t* time));

/**
 * @brief Reads the current clock tick for the given clock source type. No guarantees are made for their
 * precision or availability. If the system is in low-precision mode, the return status will be OsNotSupported
 * for the _HPC source.
 *
 * @param[In]  source  The clock source to read the tick for.
 * @param[Out] tickOut Pointer to a large integer value that can hold the current tick value.
 * @return     Returns OsOK if the tick was read, otherwise OsNotSupported.
 */
CRTDECL(OsStatus_t,
VaGetClockTick(
        _In_ enum VaClockSourceType source,
        _In_ LargeUInteger_t*       tickOut));

/**
 * @brief Reads the frequency of the clock source type. Use this to calculate the resolution of a given
 * clock source.
 *
 * @param[In]  source       The clock source to read the frequency for
 * @param[Out] frequencyOut Pointer to a large integer value that can hold the frequency value.
 * @return     Returns OsOK if the tick was read, otherwise OsNotSupported.
 */
CRTDECL(OsStatus_t,
VaGetClockFrequency(
        _In_ enum VaClockSourceType source,
        _In_ LargeUInteger_t*       frequencyOut));

/*******************************************************************************
 * Threading Extensions
 *******************************************************************************/
CRTDECL(void,       InitializeThreadParameters(ThreadParameters_t* Paramaters));
CRTDECL(OsStatus_t, SetCurrentThreadName(const char* ThreadName));
CRTDECL(OsStatus_t, GetCurrentThreadName(char* ThreadNameBuffer, size_t MaxLength));

/*******************************************************************************
 * Path Extensions
 *******************************************************************************/

/**
 * @brief Resolves the full path of the relative/incomplete path provided.
 *
 * @param[In] path The path that should be resolved into an absolute path.
 * @param[In] followSymlinks Whether links should be followed to the true path.
 * @param[In] buffer The buffer where the final path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsNotExists if the path could not be resolved.
 *         OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(OsStatus_t, GetFullPath(const char* path, int followLinks, char* buffer, size_t maxLength));

/**
 * @brief Changes the current working directory. Validation of the target path will be done
 * as a part of this call.
 *
 * @param[In] path The relative or absolute path that should be the new working directory.
 * @return OsInvalidParameters if the parameters passed were not valid.
 *         OsNotExists if the path could not be resolved
 *         OsPathIsNotDirectory If the path is not a directory
 */
CRTDECL(OsStatus_t, ChangeWorkingDirectory(const char *path));

/**
 * @brief Retrieves the current working directory.
 *
 * @param[In] buffer The buffer where the path should be stored.
 * @param[In] maxLength The size of the buffer.
 * @return OsInvalidParameters if the parameters passed were not valid.
 */
CRTDECL(OsStatus_t, GetWorkingDirectory(char* buffer, size_t maxLength));

CRTDECL(OsStatus_t, GetAssemblyDirectory(char            *buffer, size_t maxLength));
CRTDECL(OsStatus_t, GetUserDirectory(char                *buffer, size_t maxLength));
CRTDECL(OsStatus_t, GetUserCacheDirectory(char           *buffer, size_t maxLength));
CRTDECL(OsStatus_t, GetApplicationDirectory(char         *buffer, size_t maxLength));
CRTDECL(OsStatus_t, GetApplicationTemporaryDirectory(char*buffer, size_t maxLength));

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
CRTDECL(OsStatus_t, GetFilePathFromFd(int fileDescriptor, char *buffer, size_t maxLength));
CRTDECL(OsStatus_t, GetStorageInformationFromPath(const char *path, int followLinks, OsStorageDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetStorageInformationFromFd(int fileDescriptor, OsStorageDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetFileSystemInformationFromPath(const char *path, int followLinks, OsFileSystemDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetFileSystemInformationFromFd(int fileDescriptor, OsFileSystemDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetFileInformationFromPath(const char *path, int followLinks, OsFileDescriptor_t* descriptor));
CRTDECL(OsStatus_t, GetFileInformationFromFd(int fileDescriptor, OsFileDescriptor_t* descriptor));
CRTDECL(OsStatus_t, CreateFileMapping(int fileDescriptor, int flags, uint64_t offset, size_t length, void** mapping));
CRTDECL(OsStatus_t, FlushFileMapping(void* mapping, size_t length));
CRTDECL(OsStatus_t, DestroyFileMapping(void* mapping));

_CODE_END
#endif //!__MOLLENOS_H__
