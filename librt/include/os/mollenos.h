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
 * MollenOS MCore - Definitions & Structures
 * - This header describes the os-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _MOLLENOS_INTERFACE_H_
#define _MOLLENOS_INTERFACE_H_

#include <os/osdefs.h>
#include <time.h>

/* Memory Allocation Definitions
 * Flags that can be used when requesting virtual memory */
#define MEMORY_COMMIT					0x00000001
#define MEMORY_CONTIGIOUS				0x00000002
#define MEMORY_LOWFIRST					0x00000004
#define MEMORY_CLEAN					0x00000008
#define MEMORY_UNCHACHEABLE				0x00000010
#define MEMORY_READ                     0x00000020
#define MEMORY_WRITE                    0x00000040
#define MEMORY_EXECUTABLE               0x00000080

/* Memory Descriptor
 * Describes the current memory state and setup
 * thats available on the current machine */
PACKED_TYPESTRUCT(MemoryDescriptor, {
	size_t			PagesTotal;
	size_t			PagesUsed;
	size_t			PageSizeBytes;
    size_t          AllocationGranularityBytes;
});

/* Cache Type Definitions
 * Flags that can be used when requesting a flush of one of the hardware caches */
#define CACHE_INSTRUCTION               1

_CODE_BEGIN
/* MemoryAllocate
 * Allocates a chunk of memory, controlled by the
 * requested size of memory. The returned memory will always
 * be rounded up to nearest page-size */
CRTDECL(
OsStatus_t,
MemoryAllocate(
    _In_      void*         NearAddress,
	_In_      size_t        Length,
	_In_      Flags_t       Flags,
	_Out_     void**        MemoryPointer,
	_Out_Opt_ uintptr_t*    PhysicalPointer));

/* MemoryFree
 * Frees previously allocated memory and releases
 * the system resources associated. */
CRTDECL(
OsStatus_t,
MemoryFree(
	_In_ void*      MemoryPointer,
	_In_ size_t     Length));

/* MemoryProtect
 * Changes the protection flags of a previous memory allocation
 * made by MemoryAllocate */
CRTDECL(
OsStatus_t,
MemoryProtect(
    _In_  void*     MemoryPointer,
	_In_  size_t    Length,
    _In_  Flags_t   Flags,
    _Out_ Flags_t*  PreviousFlags));

/* MemoryQuery
 * Queries the underlying system for memory information 
 * like memory used and the page-size */
CRTDECL(
OsStatus_t,
MemoryQuery(
	_Out_ MemoryDescriptor_t *Descriptor));

/* SystemTime
 * Retrieves the system time. This is only ticking
 * if a system clock has been initialized. */
CRTDECL(
OsStatus_t,
SystemTime(
	_Out_ struct tm *time));

/* SystemTick
 * Retrieves the system tick counter. This is only ticking
 * if a system timer has been initialized. */
CRTDECL(
OsStatus_t,
SystemTick(
	_Out_ clock_t *clock));

/* QueryPerformanceFrequency
 * Returns how often the performance timer fires every
 * second, the value will never be 0 */
CRTDECL(
OsStatus_t,
QueryPerformanceFrequency(
	_Out_ LargeInteger_t *Frequency));

/* QueryPerformanceTimer 
 * Queries the created performance timer and returns the
 * information in the given structure */
CRTDECL(
OsStatus_t,
QueryPerformanceTimer(
	_Out_ LargeInteger_t *Value));

/* FlushHardwareCache
 * Flushes the specified hardware cache. Should be used with caution as it might
 * result in performance drops. */
CRTDECL(
OsStatus_t,
FlushHardwareCache(
    _In_     int    Cache,
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length));

/*******************************************************************************
 * Threading Extensions
 *******************************************************************************/
CRTDECL(OsStatus_t, SetCurrentThreadName(const char *ThreadName));
CRTDECL(OsStatus_t, GetCurrentThreadName(char *ThreadNameBuffer, size_t MaxLength));

/*******************************************************************************
 * Path Extensions
 *******************************************************************************/
CRTDECL(OsStatus_t, SetWorkingDirectory(const char *Path));
CRTDECL(OsStatus_t, GetWorkingDirectory(char* PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetWorkingDirectoryOfApplication(UUId_t ProcessId, char* PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetAssemblyDirectory(char *PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetUserDirectory(char *PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetUserCacheDirectory(char *PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetApplicationDirectory(char *PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetApplicationTemporaryDirectory(char *PathBuffer, size_t MaxLength));

/*******************************************************************************
 * File Extensions
 *******************************************************************************/
#define STORAGE_STATIC          0x00000001
typedef struct _vStorageDescriptor {
    long                Id;
    Flags_t             Flags;
    char                SerialNumber[32];
    LargeInteger_t      BytesTotal;
    LargeInteger_t      BytesFree;
    LargeInteger_t      BytesAvailable;
} vStorageDescriptor_t;

#define FILE_MAPPING_READ       0x00000001
#define FILE_MAPPING_WRITE      0x00000002
#define FILE_MAPPING_EXECUTE    0x00000004

#define FILE_FLAG_DIRECTORY     0x000000001

#define FILE_PERMISSION_READ    0x000000001
#define FILE_PERMISSION_WRITE   0x000000002
#define FILE_PERMISSION_EXECUTE 0x000000004
typedef struct _vFileDescriptor {
    long                Id;
    long                StorageId;
    Flags_t             Flags;
    Flags_t             Permissions;
    LargeInteger_t      Size;
    struct timespec     CreatedAt;
    struct timespec     ModifiedAt;
    struct timespec     AccessedAt;
} vFileDescriptor_t;

CRTDECL(OsStatus_t, GetFilePathFromFd(int FileDescriptor, char *PathBuffer, size_t MaxLength));
CRTDECL(OsStatus_t, GetStorageInformationFromPath(const char *Path, vStorageDescriptor_t *Information));
CRTDECL(OsStatus_t, GetStorageInformationFromFd(int FileDescriptor, vStorageDescriptor_t *Information));
CRTDECL(OsStatus_t, GetFileInformationFromPath(const char *Path, vFileDescriptor_t *Information));
CRTDECL(OsStatus_t, GetFileInformationFromFd(int FileDescriptor, vFileDescriptor_t *Information));
CRTDECL(OsStatus_t, CreateFileMapping(int FileDescriptor, int Flags, uint64_t Offset, size_t Size, void **MemoryPointer));
CRTDECL(OsStatus_t, DestroyFileMapping(void *MemoryPointer));

#if defined(i386) || defined(__i386__)
#define TLS_VALUE   uint32_t
#define TLS_READ    __asm { __asm mov ebx, [Offset] __asm mov eax, gs:[ebx] __asm mov [Value], eax }
#define TLS_WRITE   __asm { __asm mov ebx, [Offset] __asm mov eax, [Value] __asm mov gs:[ebx], eax }
#elif defined(amd64) || defined(__amd64__)
#define TLS_VALUE   uint64_t
#define TLS_READ    __asm { __asm mov rbx, [Offset] __asm mov rax, gs:[rbx] __asm mov [Value], rax }
#define TLS_WRITE   __asm { __asm mov rbx, [Offset] __asm mov rax, [Value] __asm mov gs:[rbx], rax }
#else
#error "Implement rw for tls for this architecture"
#endif

/* __get_reserved
 * Read and write the magic tls thread-specific
 * pointer, we need to take into account the compiler here */
SERVICEAPI size_t SERVICEABI
__get_reserved(size_t Index) {
	TLS_VALUE Value = 0;
	size_t Offset   = (Index * sizeof(TLS_VALUE));
	TLS_READ;
	return (size_t)Value;
}

/* __set_reserved
 * Read and write the magic tls thread-specific
 * pointer, we need to take into account the compiler here */
SERVICEAPI void SERVICEABI
__set_reserved(size_t Index, TLS_VALUE Value) {
	size_t Offset = (Index * sizeof(TLS_VALUE));
	TLS_WRITE;
}

/*******************************************************************************
 * System Extensions
 *******************************************************************************/
CRTDECL(void,       MollenOSEndBoot(void));

_CODE_END
#endif //!_MOLLENOS_INTERFACE_H_
