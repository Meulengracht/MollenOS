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
#include <ds/hashtable.h>
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

struct PEImageLoadContext {
    uuid_t     Scope;
    uuid_t     MemorySpace;
    uintptr_t  LoadAddress;
    char*      Paths;
    // NextID is the next ID assigned to the next loaded module in
    // this load context. TODO: make a bitmap and reuse id's.
    int NextID;
    mstring_t* RootModule;

    // ModuleMap is the map of loaded modules for
    // this process context. It's constructed with the following
    // format: <string, ModuleMapEntry>
    hashtable_t ModuleMap;
};

/**
 * @brief
 * @param scope
 * @param paths
 * @return
 */
struct PEImageLoadContext*
PEImageLoadContextNew(
        _In_ uuid_t      scope,
        _In_ const char* paths);

/**
 * @brief
 * @param loadContext
 */
void
PEImageLoadContextDelete(
        _In_ struct PEImageLoadContext* loadContext);

/**
 * @brief
 * @param loadContext
 * @param moduleName
 * @param modulePathOut
 * @return
 */
oserr_t
PEImageLoadContextModulePath(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 moduleName,
        _Out_ mstring_t**                modulePathOut);

/**
 * @brief
 * @param loadContext
 * @param moduleName
 * @param moduleEntryPointOut
 * @return
 */
oserr_t
PEImageLoadContextModuleEntryPoint(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 moduleName,
        _Out_ uintptr_t*                 moduleEntryPointOut);

/**
 * @brief
 * @param loadContext
 * @param path
 * @param dependency
 * @return
 */
extern oserr_t
PEImageLoad(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ mstring_t*                 path,
        _In_ bool                       dependency);

/**
 * @brief
 * @param loadContext
 * @param imageKey
 * @param force
 * @return
 */
oserr_t
PEImageUnload(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ void*                      imageKey,
        _In_ bool                       force);

/**
 * @brief
 * @param loadContext
 * @param libraryPath
 * @param imageKey
 * @return
 */
oserr_t
PEImageLoadLibrary(
        _In_  struct PEImageLoadContext* loadContext,
        _In_  mstring_t*                 libraryPath,
        _Out_ void**                     imageKeyOut,
        _Out_ uintptr_t*                 imageEntryPointOut);

/**
 * @brief
 * @param loadContext
 * @param imageKey
 * @param functionName
 * @return
 */
uintptr_t
PEImageFindExport(
        _In_ struct PEImageLoadContext* loadContext,
        _In_ void*                      imageKey,
        _In_ const char*                functionName);

/**
 * @brief
 * @param loadContext
 * @param moduleKeys
 * @param moduleCountOut
 * @return
 */
oserr_t
PEModuleKeys(
        _In_  struct PEImageLoadContext* loadContext,
        _Out_ Handle_t*                  moduleKeys,
        _Out_ int*                       moduleCountOut);

/**
 * @brief
 * @param loadContext
 * @param moduleAddresses
 * @param moduleCountOut
 * @return
 */
oserr_t
PEModuleEntryPoints(
        _In_  struct PEImageLoadContext* loadContext,
        _Out_ uintptr_t*                 moduleAddresses,
        _Out_ int*                       moduleCountOut);

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

#endif //!__PE_IMAGE_LOADER__
