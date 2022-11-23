/**
 * Copyright 2017, Philip Meulengracht
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
 * File Manager Service
 * - Handles all file related services and disk services
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
    uuid_t             ProcessID;
    uuid_t             ServerHandle;
    struct usched_cnd* Condition;
};

extern struct VFSOperations g_driverOps;

static uint64_t mod_hash(const void* element);
static int      mod_cmp(const void* element1, const void* element2);

static const char* g_modulePaths[] = {
        "/modules",
        "/initfs/modules",
        NULL
};
static hashtable_t       g_modules;
static struct usched_mtx g_modulesMutex;

oserr_t VFSInterfaceStartup(void)
{
    int status = hashtable_construct(
            &g_modules,
            HASHTABLE_MINIMUM_CAPACITY,
            sizeof(struct __ModuleEntry),
            mod_hash, mod_cmp
    );
    if (status) {
        return OS_EOOM;
    }
    usched_mtx_init(&g_modulesMutex, USCHED_MUTEX_PLAIN);
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
__TryLocateDriver(
        _In_ const char* fsType)
{
    struct __ModuleEntry  newEntry;
    struct __ModuleEntry* entry;
    struct usched_cnd     condition;
    char                  tmp[256];
    int                   i;
    uuid_t                processID = UUID_INVALID;
    uuid_t                handle    = UUID_INVALID;
    ENTRY("__TryLocateModule()");

    // Pre-initialize the module newEntry so we don't spend time after
    usched_cnd_init(&condition);
    newEntry.ServerHandle = UUID_INVALID;
    newEntry.Condition = &condition;

    i = 0;
    while (g_modulePaths[i]) {
        snprintf(&tmp[0], sizeof(tmp), "%s/%s.dll", g_modulePaths[i], fsType);
        oserr_t oserr = ProcessSpawn(&tmp[0], NULL, &processID);
        if (oserr == OS_EOK) {
            newEntry.ProcessID = processID;
            break;
        }
        i++;
    }

    if (processID == UUID_INVALID) {
        ERROR("__TryLocateModule no module could be located for %s", fsType);
        goto exit;
    }

    // wait for the module to report loaded
    usched_mtx_lock(&g_modulesMutex);
    hashtable_set(&g_modules, &newEntry);

    // TODO use timedwait here, as waiting indefinitely will be bad.
    usched_cnd_wait(&condition, &g_modulesMutex);
    entry = hashtable_get(&g_modules, &(struct __ModuleEntry) { .ProcessID = processID });
    if (entry) {
        handle = entry->ServerHandle;
        hashtable_remove(&g_modules, &(struct __ModuleEntry) { .ProcessID = processID });
    }
    usched_mtx_unlock(&g_modulesMutex);

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

    driverID = __TryLocateDriver(type);
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
    struct __ModuleEntry* entry;
    _CRT_UNUSED(message);

    usched_mtx_lock(&g_modulesMutex);
    entry = hashtable_get(&g_modules, &(struct __ModuleEntry) { .ProcessID = processId });
    if (entry) {
        entry->ServerHandle = serverHandle;
        usched_cnd_notify_one(entry->Condition);
    } else {
        WARNING("sys_file_fsready_invocation module was not present, hang is now ocurring");
    }
    usched_mtx_unlock(&g_modulesMutex);
}

static uint64_t mod_hash(const void* element)
{
    const struct __ModuleEntry* entry = element;
    return entry->ProcessID;
}

static int mod_cmp(const void* element1, const void* element2)
{
    const struct __ModuleEntry* entry1 = element1;
    const struct __ModuleEntry* entry2 = element2;
    return entry1->ProcessID == entry2->ProcessID ? 0 : -1;
}
