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

#define __TRACE

#include <ddk/convert.h>
#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <os/services/process.h>
#include <os/usched/cond.h>
#include <stdio.h>
#include <vfs/interface.h>

struct __ModuleEntry {
    const char* Type;
    bool        Loaded;
    uuid_t      ServerHandle;
};

struct __ModuleLoaderEntry {
    uuid_t             ProcessID;
    uuid_t             ServerHandle;
    struct usched_cnd* Condition;
};

extern struct VFSOperations g_driverOps;

static uint64_t mod_load_hash(const void* element);
static int      mod_load_cmp(const void* element1, const void* element2);

static const char* g_modulePaths[] = {
        "/modules",
        "/initfs/modules",
        NULL
};
static hashtable_t       g_modules;
static struct usched_mtx g_modulesMutex;
static struct usched_cnd g_modulesCondition;

static hashtable_t       g_moduleLoaders;
static struct usched_mtx g_moduleLoadersMutex;

oserr_t VFSInterfaceStartup(void)
{
    int status = hashtable_construct(
            &g_moduleLoaders,
            HASHTABLE_MINIMUM_CAPACITY,
            sizeof(struct __ModuleLoaderEntry),
            mod_load_hash, mod_load_cmp
    );
    if (status) {
        return OS_EOOM;
    }
    usched_mtx_init(&g_moduleLoadersMutex, USCHED_MUTEX_PLAIN);
    return OS_EOK;
}

struct VFSInterface*
VFSInterfaceNew(
        _In_  uuid_t                driverID,
        _In_  struct VFSOperations* operations)
{
    struct VFSInterface* interface;

    interface = (struct VFSInterface*)malloc(sizeof(struct VFSInterface));
    if (!interface) {
        return NULL;
    }

    interface->DriverID = driverID;
    usched_mtx_init(&interface->Lock, USCHED_MUTEX_PLAIN);
    if (operations) {
        memcpy(&interface->Operations, operations, sizeof(struct VFSOperations));
    }
    return interface;
}

static uuid_t
__TryLoadModule(
        _In_ const char* fsType)
{
    struct __ModuleLoaderEntry  newEntry;
    struct __ModuleLoaderEntry* entry;
    struct usched_cnd           condition;
    char                        tmp[256];
    int                         i;
    uuid_t                      processID = UUID_INVALID;
    uuid_t                      handle    = UUID_INVALID;
    ENTRY("__TryLoadModule()");

    // Pre-initialize the module newEntry so we don't spend time after
    usched_cnd_init(&condition);
    newEntry.ServerHandle = UUID_INVALID;
    newEntry.Condition = &condition;

    i = 0;
    while (g_modulePaths[i]) {
        snprintf(&tmp[0], sizeof(tmp), "%s/%s.dll", g_modulePaths[i], fsType);
        oserr_t oserr = OSProcessSpawn(&tmp[0], NULL, &processID);
        if (oserr == OS_EOK) {
            newEntry.ProcessID = processID;
            break;
        }
        i++;
    }

    if (processID == UUID_INVALID) {
        ERROR("__TryLoadModule no module could be located for %s", fsType);
        goto exit;
    }

    // wait for the module to report loaded
    usched_mtx_lock(&g_moduleLoadersMutex);
    entry = hashtable_get(&g_moduleLoaders, &(struct __ModuleLoaderEntry) { .ProcessID = processID });
    if (!entry) {
        hashtable_set(&g_moduleLoaders, &newEntry);

        // TODO use timedwait here, as waiting indefinitely will be bad.
        usched_cnd_wait(&condition, &g_moduleLoadersMutex);
        entry = hashtable_get(&g_moduleLoaders, &(struct __ModuleLoaderEntry) { .ProcessID = processID });
    }

    if (entry) {
        handle = entry->ServerHandle;
        hashtable_remove(&g_moduleLoaders, &(struct __ModuleLoaderEntry) { .ProcessID = processID });
    }
    usched_mtx_unlock(&g_moduleLoadersMutex);

exit:
    EXIT("__TryLocateModule");
    return handle;
}

oserr_t
VFSInterfaceLoadInternal(
        _In_  const char*           type,
        _Out_ struct VFSInterface** interfaceOut)
{
	uuid_t driverID;
    TRACE("VFSInterfaceLoadInternal(%s)", type);

	if (type == NULL || interfaceOut == NULL) {
	    return OS_EINVALPARAMS;
	}

    driverID = __TryLoadModule(type);
    if (driverID == UUID_INVALID) {
        ERROR("VFSInterfaceLoadInternal failed to load %s", type);
        return OS_ENOENT;
    }
    return VFSInterfaceLoadDriver(driverID, interfaceOut);
}

oserr_t
VFSInterfaceLoadDriver(
        _In_  uuid_t                 interfaceDriverID,
        _Out_ struct VFSInterface**  interfaceOut)
{
    struct VFSInterface* interface;
    TRACE("VFSInterfaceLoadDriver(id=%u)", interfaceDriverID);

    if (interfaceDriverID == UUID_INVALID || interfaceOut == NULL) {
        return OS_EINVALPARAMS;
    }

    interface = VFSInterfaceNew(interfaceDriverID, &g_driverOps);
    if (interface == NULL) {
        return OS_EOOM;
    }
    *interfaceOut = interface;
    return OS_EOK;
}

void
VFSInterfaceDelete(
        _In_ struct VFSInterface* interface)
{
    if (!interface) {
        return;
    }
    free(interface);
}

void sys_file_fsready_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t serverHandle)
{
    struct __ModuleLoaderEntry* entry;
    _CRT_UNUSED(message);

    usched_mtx_lock(&g_moduleLoadersMutex);
    entry = hashtable_get(&g_moduleLoaders, &(struct __ModuleLoaderEntry) { .ProcessID = processId });
    if (entry) {
        entry->ServerHandle = serverHandle;
        usched_cnd_notify_one(entry->Condition);
    } else {
        hashtable_set(&g_moduleLoaders, &(struct __ModuleLoaderEntry) {
            .ProcessID = processId,
            .ServerHandle = serverHandle,
            .Condition = NULL
        });
    }
    usched_mtx_unlock(&g_moduleLoadersMutex);
}

static uint64_t mod_load_hash(const void* element)
{
    const struct __ModuleLoaderEntry* entry = element;
    return entry->ProcessID;
}

static int mod_load_cmp(const void* element1, const void* element2)
{
    const struct __ModuleLoaderEntry* entry1 = element1;
    const struct __ModuleLoaderEntry* entry2 = element2;
    return entry1->ProcessID == entry2->ProcessID ? 0 : -1;
}
