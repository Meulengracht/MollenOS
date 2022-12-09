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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
#include <ds/mstring.h>
#include <os/pe.h>
#include <time.h>

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
    uuid_t                Owner;
    mstring_t*            Name;
    mstring_t*            FullPath;
    atomic_int            References;
    uuid_t                MemorySpace;
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
struct MemoryMappingState {
    uuid_t       Handle;
    uintptr_t    Address;
    size_t       Length;
    unsigned int Flags;
};

__EXTERN uintptr_t  PeImplGetPageSize(void);
__EXTERN uintptr_t  PeImplGetBaseAddress(void);
__EXTERN clock_t    PeImplGetTimestampMs(void);
__EXTERN oserr_t PeImplResolveFilePath(uuid_t, mstring_t*, mstring_t*, mstring_t**);
__EXTERN oserr_t PELoadImage(mstring_t*, void**, size_t*);
__EXTERN oserr_t PeImplCreateImageSpace(uuid_t* handleOut);

/*******************************************************************************
 * Public API 
 *******************************************************************************/

/**
 * @brief Validates a file-buffer of the given length, does initial header checks and
 * performs a checksum validation.
 *
 * @param buffer
 * @param length
 * @param checksumOut
 * @return
 */
extern oserr_t
PEValidateImageChecksum(
        _In_  uint8_t*  buffer,
        _In_  size_t    length,
        _Out_ uint32_t* checksumOut);

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
extern oserr_t
PeLoadImage(
        _In_  uuid_t           owner,
        _In_  PeExecutable_t*  parent,
        _In_  mstring_t*       path,
        _Out_ PeExecutable_t** imageOut);

/**
 * @brief Unload executables, all it's dependancies and free it's resources
 *
 * @param image
 * @return
 */
extern oserr_t
PeUnloadImage(
    _In_ PeExecutable_t* image);

/* PeUnloadLibrary
 * Unload dynamically loaded library 
 * This only cleans up in the case there are no more references */
extern oserr_t
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
    _In_    mstring_t*      LibraryName);

/* PeResolveFunction
 * Resolves a function by name in the given pe image, the return
 * value is the address of the function. 0 If not found */
__EXTERN uintptr_t
PeResolveFunction(
    _In_ PeExecutable_t* Library, 
    _In_ const char*     Function);

/* PeGetModuleHandles
 * Retrieves a list of loaded module handles currently loaded for the process. */
__EXTERN oserr_t
PeGetModuleHandles(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount);

/* PeGetModuleEntryPoints
 * Retrieves a list of loaded module entry points currently loaded for the process. */
__EXTERN oserr_t
PeGetModuleEntryPoints(
    _In_  PeExecutable_t* executable,
    _Out_ Handle_t*       moduleList,
    _Out_ int*            moduleCount);

#endif //!__PE_IMAGE_LOADER__
