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
 * MollenOS MCore - PE Format Loader
 */

#ifndef __MCORE_PELOADER__
#define __MCORE_PELOADER__

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <ds/collection.h>
#include <ds/mstring.h>

/* Includes
 * - System */
#include <os/process.h>

/* We define some helpers in order to determine 
 * some information about the current build, this
 * is so we can do proper validation of the loaded
 * binary */
#if defined(i386) || defined(__i386__)
#define PE_CURRENT_MACHINE                  PE_MACHINE_X32
#define PE_CURRENT_ARCH                     PE_ARCHITECTURE_32
#elif defined(amd64) || defined(__amd64__)
#define PE_CURRENT_MACHINE                  PE_MACHINE_X64
#define PE_CURRENT_ARCH                     PE_ARCHITECTURE_64
#else
#error "Unhandled PE architecture used"
#endif

/* The Pe-Image file exports a number of functions
 * and to avoid re-parsing every time we want to resolve
 * a function we cache them here, an exported function consists
 * of a name, ordinal and where they reside in memory */
typedef struct _MCorePeExportFunction {
    char*                   Name;
    char*                   ForwardName; //Library.Function
    int                     Ordinal;
    uintptr_t               Address;
} MCorePeExportFunction_t;

/* The Pe-Image file structure, this contains the
 * loaded binaries and libraries, the functions an 
 * image exports and base-information */
typedef struct _MCorePeFile {
    MString_t               *Name;
    uint32_t                 Architecture;
    int                      References;
    int                      UsingInitRD;

    uintptr_t                VirtualAddress;
    uintptr_t                EntryAddress;
    uintptr_t                CodeBase;
    size_t                   CodeSize;
    
    int                      NumberOfExportedFunctions;
    MCorePeExportFunction_t *ExportedFunctions;
    Collection_t            *LoadedLibraries;
    Spinlock_t               LibraryLock;
} MCorePeFile_t;

/* PeValidate
 * Validates a file-buffer of the given length,
 * does initial header checks and performs a checksum
 * validation. Returns either PE_INVALID or PE_VALID */
__EXTERN 
int
PeValidate(
    _In_ uint8_t *Buffer, 
    _In_ size_t Length);

/* PeResolveLibrary
 * Resolves a dependancy or a given module path, a load address must be provided
 * together with a pe-file header to fill out and the parent that wants to resolve
 * the library */
__EXTERN
MCorePeFile_t*
PeResolveLibrary(
    _In_ MCorePeFile_t *Parent,
    _In_ MCorePeFile_t *PeFile,
    _In_ MString_t *LibraryName,
    _InOut_ uintptr_t *LoadAddress);

/* PeResolveFunction
 * Resolves a function by name in the given pe image, the return
 * value is the address of the function. 0 If not found */
__EXTERN
uintptr_t
PeResolveFunction(
    _In_ MCorePeFile_t *Library, 
    _In_ __CONST char *Function);

/* PeLoadImage
 * Loads the given file-buffer as a pe image into the current address space 
 * at the given Base-Address, which is updated after load to reflect where
 * the next address is available for load */
__EXTERN
MCorePeFile_t*
PeLoadImage(
    _In_ MCorePeFile_t *Parent, 
    _In_ MString_t *Name, 
    _In_ uint8_t *Buffer, 
    _In_ size_t Length, 
    _InOut_ uintptr_t *BaseAddress, 
    _In_ int UsingInitRD);

/* PeUnloadLibrary
 * Unload dynamically loaded library 
 * This only cleans up in the case there are no more references */
__EXTERN
OsStatus_t
PeUnloadLibrary(
    _In_ MCorePeFile_t *Parent, 
    _In_ MCorePeFile_t *Library);

/* PeUnloadImage
 * Unload executables, all it's dependancies and free it's resources */
__EXTERN
OsStatus_t
PeUnloadImage(
    _In_ MCorePeFile_t *Executable);

/* PeGetModuleHandles
 * Retrieves a list of loaded module handles currently loaded for the process. */
KERNELAPI
OsStatus_t
KERNELABI
PeGetModuleHandles(
    _In_ MCorePeFile_t *Executable,
    _Out_ Handle_t ModuleList[PROCESS_MAXMODULES]);

/* PeGetModuleEntryPoints
 * Retrieves a list of loaded module entry points currently loaded for the process. */
KERNELAPI
OsStatus_t
KERNELABI
PeGetModuleEntryPoints(
    _In_ MCorePeFile_t *Executable,
    _Out_ Handle_t ModuleList[PROCESS_MAXMODULES]);

#endif //!__MCORE_PELOADER__
