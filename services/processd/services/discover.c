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

//#define __TRACE

#define __need_minmax
#include <errno.h>
#include <ddk/initrd.h>
#include <ddk/utils.h>
#include <discover.h>
#include <os/memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process.h"
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>

static uint64_t __svc_hash(const void* element);
static int      __svc_cmp(const void* element1, const void* element2);

static struct VaFs* g_vafs          = NULL;
static void*        g_ramdiskBuffer = NULL;
static size_t       g_ramdiskSize   = 0;
static const char*  g_svcFlatEnvironment = "LDPATH=/initfs/bin\0";
static int          g_serviceID     = 1;
static hashtable_t  g_services;

static int
__EndsWith(
        _In_ const char* text,
        _In_ const char* suffix)
{
    size_t lengthOfText;
    size_t lengthOfSuffix;

    if (!text || !suffix){
        return 0;
    }

    lengthOfText   = strlen(text);
    lengthOfSuffix = strlen(suffix);
    if (lengthOfSuffix > lengthOfText) {
        return 0;
    }

    return strncmp(text + lengthOfText - lengthOfSuffix, suffix, lengthOfSuffix);
}

static oserr_t
__ReadFile(
        _In_  struct VaFsDirectoryHandle* directoryHandle,
        _In_  const char*                 filename,
        _Out_ void**                      bufferOut,
        _Out_ size_t*                     lengthOut)
{
    struct VaFsFileHandle* fileHandle;
    size_t                 bytesRead;
    size_t                 fileSize;
    void*                  fileBuffer;
    int                    status;

    // now lets access the file
    status = vafs_directory_open_file(directoryHandle, filename, &fileHandle);
    if (status) {
        ERROR("__ReadFile file %s was not found", filename);
        return OS_ENOENT;
    }

    // allocate a buffer for the file, and read the data
    fileSize = vafs_file_length(fileHandle);
    fileBuffer = malloc(fileSize);
    if (!fileBuffer) {
        return OS_EUNKNOWN;
    }

    bytesRead = vafs_file_read(fileHandle, fileBuffer, fileSize);
    if (bytesRead != fileSize) {
        WARNING("__ReadFile read %" PRIuIN "/%" PRIuIN " bytes from file", bytesRead, fileSize);
    }

    vafs_file_close(fileHandle);
    *bufferOut = fileBuffer;
    *lengthOut = fileSize;
    return OS_EOK;
}

static mstring_t*
__ServiceNameFromYaml(
        _In_ const char* yamlName)
{
    // Replace .yaml with .dll
    mstring_t* result;
    mstring_t* name = mstr_new_u8(yamlName);
    if (name == NULL) {
        return NULL;
    }

    result = mstr_path_change_extension_u8(name, ".dll");
    mstr_delete(name);
    return result;
}

static void
__SystemServiceDelete(
        _In_ struct SystemService* systemService)
{
    if (systemService == NULL) {
        return;
    }

    mstr_delete(systemService->Name);
    mstr_delete(systemService->Path);
    mstr_delete(systemService->APIPath);
    free(systemService);
}

static struct SystemService*
__SystemServiceNew(
        _In_ const char* yamlName)
{
    struct SystemService* systemService;

    systemService = malloc(sizeof(struct SystemService));
    if (systemService == NULL) {
        return NULL;
    }
    memset(systemService, 0, sizeof(struct SystemService));

    systemService->Name = __ServiceNameFromYaml(yamlName);
    if (systemService->Name == NULL) {
        free(systemService);
        return NULL;
    }

    systemService->Path = mstr_fmt("/initfs/services/%ms", systemService->Name);
    if (systemService->Path == NULL) {
        __SystemServiceDelete(systemService);
        return NULL;
    }
    return systemService;
}

static void
__RegisterSystemService(
        _In_ struct SystemService* systemService)
{
    // Assign an idea before we insert
    systemService->ID = g_serviceID++;
    hashtable_set(&g_services, systemService);
}

static oserr_t
__ParseServiceConfiguration(
        _In_ struct VaFsDirectoryHandle* directoryHandle,
        _In_ const char*                 name)
{
    struct SystemService* systemService;
    oserr_t               oserr;
    void*                 buffer;
    size_t                length;
    TRACE("__ParseServiceConfiguration(name=%s)", name);

    oserr = __ReadFile(directoryHandle, name, &buffer, &length);
    if (oserr != OS_EOK) {
        return oserr;
    }

    systemService = __SystemServiceNew(name);
    if (!systemService) {
        free(buffer);
        return OS_EOOM;
    }

    oserr = PSParseServiceYAML(systemService, buffer, length);
    free(buffer);

    if (oserr != OS_EOK) {
        free(systemService);
        return oserr;
    }
    __RegisterSystemService(systemService);
    return oserr;
}

static char*
__BuildArguments(
        _In_ const struct SystemService* systemService)
{
    // Always allocate a base length of 128 to cover argument specifiers.
    size_t argsLength = 128;
    char*  args;

    // TODO: we should convert the mstring to a C string before taking the length, this should
    // however in almost any case be OK and allocate enought space.
    argsLength += mstr_bsize(systemService->APIPath);
    args = malloc(argsLength);
    if (args == NULL) {
        return NULL;
    }

    snprintf(args, argsLength - 1, "--api-path %ms", systemService->APIPath);
    return args;
}

static void
__SpawnService(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct SystemService* systemService = element;
    struct ProcessOptions*      procOpts      = userContext;
    oserr_t                     oserr;
    uuid_t                      handle;
    char*                       path;
    char*                       args;
    _CRT_UNUSED(index);

    path = mstr_u8(systemService->Path);
    if (path == NULL) {
        ERROR("__SpawnService failed to allocate memory for path");
        return;
    }

    args = __BuildArguments(systemService);
    if (args == NULL) {
        ERROR("__SpawnService failed to allocate memory for arguments");
        free(path);
        return;
    }

    TRACE("__SpawnService %s", &pathBuffer[0]);
    oserr = PmCreateProcess(path, args, procOpts, &handle);
    if (oserr != OS_EOK) {
        ERROR("__SpawnService failed to spawn service %s", path);
    }
    free(path);
    free(args);
}

static void
__SpawnServices(void)
{
    struct ProcessOptions procOpts;

    // Carefully construct the process options structure. We have to be
    // a bit hacky now some data is moved onto the dma buffer.
    procOpts.Scope = UUID_INVALID;
    procOpts.MemoryLimit.QuadPart = 0;
    procOpts.WorkingDirectory = NULL;
    procOpts.InheritationBlockLength = 0;
    // TODO: if the environment gets more keys we need to write a function
    procOpts.EnvironmentBlockLength = strlen(g_svcFlatEnvironment) + 2; // two terminating zeroes
    procOpts.DataBuffer = (const void*)g_svcFlatEnvironment;

    // TODO: resolve by dependencies. do exactly like we do with PE images
    hashtable_enumerate(&g_services, __SpawnService, &procOpts);
}

static oserr_t
__ParseRamdisk(
        _In_ void*  ramdiskBuffer,
        _In_ size_t ramdiskSize)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFsEntry            entry;
    int                         status;
    oserr_t                     oserr;
    TRACE("__ParseRamdisk(buffer=0x%llx, size=%llu)", ramdiskBuffer, ramdiskSize);

    status = vafs_open_memory(ramdiskBuffer, ramdiskSize, &g_vafs);
    if (status) {
        ERROR("__ParseRamdisk failed to open vafs image: %i", errno);
        return OS_EUNKNOWN;
    }

    status = DdkInitrdHandleVafsFilter(g_vafs);
    if (status) {
        ERROR("__ParseRamdisk vafs image is using an unsupported filter");
        vafs_close(g_vafs);
        return OS_ENOTSUPPORTED;
    }

    status = vafs_directory_open(g_vafs, "/services", &directoryHandle);
    if (status) {
        ERROR("__ParseRamdisk failed to open /services in vafs image");
        vafs_close(g_vafs);
        return OS_ENOTSUPPORTED;
    }

    while (vafs_directory_read(directoryHandle, &entry) == 0) {
        TRACE("__ParseRamdisk found entry %s", entry.Name);
        // Skip all entries that are not regular files
        if (entry.Type != VaFsEntryType_File) {
            continue;
        }

        // Skip any file that is not a yaml
        if (__EndsWith(entry.Name, ".yaml") != 0) {
            continue;
        }

        // Parse the YAML configuration to check for valid service entry
        oserr = __ParseServiceConfiguration(directoryHandle, entry.Name);
        if (oserr != OS_EOK) {
            WARNING("__ParseRamdisk failed to parse service confguration %s: %u", entry.Name, oserr);
        }
    }

    // close the directory and cleanup
    vafs_directory_close(directoryHandle);
    return OS_EOK;
}

void
PSBootstrap(
        void* __unused0,
        void* __unused1)
{
    oserr_t oserr;
    void*   ramdisk;
    size_t  ramdiskSize;
    _CRT_UNUSED(__unused0);
    _CRT_UNUSED(__unused1);
    TRACE("PSBootstrap()");

    hashtable_construct(
            &g_services,
            0,
            sizeof(struct SystemService),
            __svc_hash, __svc_cmp
    );

    // Let's map in the ramdisk and discover various service modules
    oserr = DdkUtilsMapRamdisk(&ramdisk, &ramdiskSize);
    if (oserr != OS_EOK) {
        TRACE("ProcessBootstrap failed to map ramdisk into address space %u", oserr);
        return;
    }

    // store buffer and size for later cleanup
    g_ramdiskBuffer = ramdisk;
    g_ramdiskSize   = ramdiskSize;

    oserr = __ParseRamdisk(ramdisk, ramdiskSize);
    if (oserr != OS_EOK) {
        ERROR("ProcessBootstrap failed to parse ramdisk: %u", oserr);
        return;
    }
    __SpawnServices();
}

void
PSBootstrapCleanup(void)
{
    oserr_t osStatus;

    // close the vafs handle before freeing the buffer
    vafs_close(g_vafs);

    if (g_ramdiskBuffer && g_ramdiskSize) {
        osStatus = MemoryFree(g_ramdiskBuffer, g_ramdiskSize);
        if (osStatus != OS_EOK) {
            ERROR("PmBootstrapCleanup failed to free the ramdisk memory");
        }
    }
}

oserr_t
PmBootstrapFindRamdiskFile(
        _In_  mstring_t* path,
        _Out_ void**     bufferOut,
        _Out_ size_t*    bufferSizeOut)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFsFileHandle*      fileHandle;
    char*                       internPath;
    char*                       internFilename;
    int                         status;
    char                        tempbuf[64] = { 0 };
    char*                       pathu8;

    pathu8 = mstr_u8(path);
    if (pathu8 == NULL) {
        return OS_EOOM;
    }

    TRACE("PmBootstrapFindRamdiskFile(path=%s)", pathu8);

    // skip the /initfs/ prefix
    internPath = strchr(pathu8 + 1, '/');
    internFilename = strrchr(pathu8, '/');

    // Ok, so max out at len(tempbuf) - 1, but minimum 1 char to include the initial '/'
    strncpy(
            &tempbuf[0],
            internPath,
            MAX(1, MIN(internFilename - internPath, sizeof(tempbuf) - 1))
    );

    // skip the starting '/'
    internFilename++;

    status = vafs_directory_open(g_vafs, &tempbuf[0], &directoryHandle);
    if (status) {
        ERROR("PmBootstrapFindRamdiskFile failed to open %s: %i", &tempbuf[0], errno);
        free(pathu8);
        return OS_EUNKNOWN;
    }

    // now lets access the file
    status = vafs_directory_open_file(directoryHandle, internFilename, &fileHandle);
    if (status) {
        WARNING("PmBootstrapFindRamdiskFile file %s was not found", internFilename);
        free(pathu8);
        return OS_ENOENT;
    }

    // allocate a buffer for the file, and read the data
    if (bufferOut && bufferSizeOut)
    {
        size_t bytesRead;
        size_t fileSize = vafs_file_length(fileHandle);
        void*  fileBuffer = malloc(fileSize);
        if (!fileBuffer) {
            free(pathu8);
            return OS_EUNKNOWN;
        }

        bytesRead = vafs_file_read(fileHandle, fileBuffer, fileSize);
        if (bytesRead != fileSize) {
            WARNING("PmBootstrapFindRamdiskFile read %" PRIuIN "/%" PRIuIN " bytes from file", bytesRead, fileSize);
        }

        *bufferOut = fileBuffer;
        *bufferSizeOut = fileSize;
    }

    // close the file and directory handle
    vafs_file_close(fileHandle);
    vafs_directory_close(directoryHandle);
    free(pathu8);
    return OS_EOK;
}

static uint64_t __svc_hash(const void* element)
{
    const struct SystemService* systemService = element;
    return systemService->ID;
}

static int __svc_cmp(const void* element1, const void* element2)
{
    _CRT_UNUSED(element1);
    _CRT_UNUSED(element2);
    // We can safely assume the uniquness of the ID. So always return 0 here.
    return 0;
}
