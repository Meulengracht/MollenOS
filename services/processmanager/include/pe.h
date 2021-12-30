/**
 * Copyright 2018, Philip Meulengracht
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
 * PE/COFF Image Loader
 *    - Implements support for loading and processing pe/coff image formats
 *      and implemented as a part of libds to share between services and kernel
 */

#ifndef __PE_IMAGE_LOADER__
#define __PE_IMAGE_LOADER__

#include <os/osdefs.h>
#include <os/types/process.h>
#include <ds/list.h>
#include <os/pe.h>
#include <time.h>

DECL_STRUCT(MString);
typedef void* MemorySpaceHandle_t;
typedef void* MemoryMapHandle_t;

#if defined(i386) || defined(__i386__)
#define PE_CURRENT_MACHINE                  PE_MACHINE_X32
#define PE_CURRENT_ARCH                     PE_ARCHITECTURE_32
#elif defined(amd64) || defined(__amd64__)
#define PE_CURRENT_MACHINE                  PE_MACHINE_X64
#define PE_CURRENT_ARCH                     PE_ARCHITECTURE_64
#else
#error "Unhandled PE architecture used"
#endif

typedef struct PeExportedFunction {
    const char* Name;
    const char* ForwardName; //Library.Function
    int         Ordinal;
    uintptr_t   Address;
} PeExportedFunction_t;

typedef struct PeExecutable {
    UUId_t                Owner;
    MString_t*            Name;
    MString_t*            FullPath;
    atomic_int            References;
    MemorySpaceHandle_t   MemorySpace;
    element_t             Header;

    uint32_t              Architecture;

    uintptr_t             VirtualAddress;
    uintptr_t             EntryAddress;
    uintptr_t             OriginalImageBase;
    uintptr_t             CodeBase;
    size_t                CodeSize;
    uintptr_t             NextLoadingAddress;
    
    int                   NumberOfExportedFunctions;
    PeExportedFunction_t* ExportedFunctions;
    char*                 ExportedFunctionNames;
    list_t*               Libraries;
} PeExecutable_t;

/*******************************************************************************
 * Support Methods 
 *******************************************************************************/
__EXTERN uintptr_t  PeImplGetPageSize(void);
__EXTERN uintptr_t  PeImplGetBaseAddress(void);
__EXTERN clock_t    PeImplGetTimestamp(void);
__EXTERN OsStatus_t PeImplResolveFilePath(UUId_t, MString_t*, MString_t*, MString_t**);
__EXTERN OsStatus_t PeImplLoadFile(MString_t*, void**, size_t*);
__EXTERN void       PeImplUnloadFile(MString_t*, void*);
__EXTERN OsStatus_t PeImplCreateImageSpace(MemorySpaceHandle_t* handleOut);
__EXTERN OsStatus_t PeImplAcquireImageMapping(MemorySpaceHandle_t memorySpaceHandle, uintptr_t* address, size_t length, unsigned int flags, MemoryMapHandle_t* handleOut);
__EXTERN void       PeImplReleaseImageMapping(MemoryMapHandle_t mapHandle);

/*******************************************************************************
 * Public API 
 *******************************************************************************/

/**
 * @brief Validates a file-buffer of the given length, does initial header checks and
 * performs a checksum validation.
 *
 * @param Buffer
 * @param Length
 * @return
 */
extern OsStatus_t
PeValidateImageBuffer(
    _In_ uint8_t* Buffer,
    _In_ size_t   Length);

/**
 * @brief Loads the given file-buffer as a pe image into the current address space
 * at the given Base-Address, which is updated after load to reflect where
 * the next address is available for load
 *
 * @param[In] owner
 * @param[In] parent
 * @param[In] path
 * @param[Out] imageOut
 * @return
 */
extern OsStatus_t
PeLoadImage(
    _In_  UUId_t           owner,
    _In_  PeExecutable_t*  parent,
    _In_  MString_t*       path,
    _Out_ PeExecutable_t** imageOut);

/**
 * @brief Unload executables, all it's dependancies and free it's resources
 *
 * @param image
 * @return
 */
extern OsStatus_t
PeUnloadImage(
    _In_ PeExecutable_t* image);

/* PeUnloadLibrary
 * Unload dynamically loaded library 
 * This only cleans up in the case there are no more references */
extern OsStatus_t
PeUnloadLibrary(
    _In_ PeExecutable_t* parent,
    _In_ PeExecutable_t* library);

/* PeResolveLibrary
 * Resolves a dependancy or a given module path, a load address must be provided
 * together with a pe-file header to fill out and the parent that wants to resolve the library */
__EXTERN PeExecutable_t*
PeResolveLibrary(
    _In_    PeExecutable_t* Parent,
    _In_    PeExecutable_t* Image,
    _In_    MString_t*      LibraryName);

/* PeResolveFunction
 * Resolves a function by name in the given pe image, the return
 * value is the address of the function. 0 If not found */
__EXTERN uintptr_t
PeResolveFunction(
    _In_ PeExecutable_t* Library, 
    _In_ const char*     Function);

/* PeGetModuleHandles
 * Retrieves a list of loaded module handles currently loaded for the process. */
__EXTERN OsStatus_t
PeGetModuleHandles(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount);

/* PeGetModuleEntryPoints
 * Retrieves a list of loaded module entry points currently loaded for the process. */
__EXTERN OsStatus_t
PeGetModuleEntryPoints(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount);

#endif //!__PE_IMAGE_LOADER__
