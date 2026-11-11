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
#define __need_quantity

#include <assert.h>
#include <ddk/service.h>
#include <ddk/convert/grants.h>
#include <ddk/utils.h>
#include <errno.h>
#include <gracht/link/vali.h>
#include <internal/_io.h>
#include <internal/_gracht.h>
#include <internal/_shm_transfer.h>
#include <os/internal/_process.h>
#include <internal/_utils.h>
#include <internal/_tls.h>
#include <os/handle.h>
#include <os/grants.h>
#include <os/memory.h>
#include <os/shm.h>
#include <os/threads.h>
#include <os/usched/xunit.h>
#include <os/usched/job.h>
#include <os/usched/usched.h>
#include <stdlib.h>
#include <string.h>
#include <sys_process_service_client.h>
#include <time.h>

extern void StdioInitialize(void);
extern void StdioConfigureStandardHandles(void);
extern void StdSignalInitialize(void);
extern void StdMmapInitialize(void);
extern void OSHandlesInitialize(void);

// The default inbuilt client for rpc communication. In general this should only be used
// internally for calls to services and modules.
static gracht_client_t*         g_gclient     = NULL;
static struct gracht_link_vali* g_gclientLink = NULL;

static const char* g_nullEnvironment[] = {
        NULL
};

static char*      g_rawCommandLine  = NULL;
static uintptr_t* g_baseLibraries   = NULL;
static char**     g_environment     = NULL;
static int        g_isPhoenix       = 0;
static uuid_t     g_startupThreadId = UUID_INVALID;
static uuid_t     g_processId       = UUID_INVALID;

static void
__mark_iod_priority(int iod)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    if (!handle) {
        return;
    }
    stdio_handle_set_flags(handle, __IO_PRIORITY);
}

static void
__free_environment(
    _In_ char** environment)
{
    if (environment == NULL) {
        return;
    }
    
    for (size_t i = 0; environment[i] != NULL; i++) {
        free(environment[i]);
    }
    free(environment);
}

static oserr_t
__build_environment(
    _In_  const char* environment,
    _In_  size_t      length,
    _Out_ char***     environmentOut)
{
    char**  pairs;
    size_t  offset = 0;
    size_t  count;
    size_t  entryLength;
    oserr_t oserr;

    oserr = __EnvironmentBlockValidate(environment, length, &count);
    if (oserr != OS_EOK) {
        return oserr;
    }

    pairs = calloc(count + 1, sizeof(char*));
    if (pairs == NULL) {
        return OS_EOOM;
    }

    for (size_t i = 0; i < count; i++) {
        entryLength = strlen(environment + offset);
        pairs[i] = strndup(environment + offset, entryLength);
        if (pairs[i] == NULL) {
            __free_environment(pairs);
            return OS_EOOM;
        }
        offset += entryLength + 1;
    }

    *environmentOut = pairs;
    return OS_EOK;
}

static oserr_t
__parse_library_handles(
    _In_  const char* buffer,
    _In_  size_t      length,
    _Out_ uintptr_t** handlesOut)
{
    size_t     entries = length / sizeof(uintptr_t);
    uintptr_t* libraries;

    if (length % sizeof(uintptr_t) != 0 || entries >= PROCESS_MAXMODULES) {
        return OS_EINVALPARAMS;
    }

    if (entries == 0) {
        return OS_ENOENT;
    }
    
    libraries = calloc(entries + 1, sizeof(uintptr_t));
    if (libraries == NULL) {
        return OS_EOOM;
    }

    // Arguments have byte lengths, so the following entry list does not need to
    // to be aligned.
    memcpy(libraries, buffer, length);
    *handlesOut = libraries;
    return OS_EOK;
}

static oserr_t
__parse_startup_info(
    _In_ OSHandle_t* osHandle,
    _In_ uint32_t    bytesWritten)
{
    struct ProcessStartupSections sections;
    char*                         arguments = NULL;
    uintptr_t*                    libraries = NULL;
    char**                        environment = NULL;
    oserr_t                       oserr;

    if (bytesWritten > PROCESS_STARTUP_MAX_SIZE ||
        bytesWritten > SHMBufferCapacity(osHandle)) {
        return OS_EBUFFER;
    }

    oserr = __ProcessStartupSplit(SHMBuffer(osHandle), bytesWritten, &sections);
    if (oserr != OS_EOK) {
        return oserr;
    }

    arguments = strndup(sections.Arguments, sections.Header.ArgumentsLength);
    if (arguments == NULL) {
        return OS_EOOM;
    }

    oserr = __parse_library_handles(
        sections.LibraryEntries,
        sections.Header.LibraryEntriesLength,
        &libraries
    );
    if (oserr != OS_EOK) {
        goto error;
    }

    if (sections.Header.EnvironmentBlockLength != 0) {
        oserr = __build_environment(
            sections.Environment,
            sections.Header.EnvironmentBlockLength,
            &environment
        );
        if (oserr != OS_EOK) {
            goto error;
        }
    }

    oserr = CRTReadInheritanceBlock(
        sections.Inheritance,
        sections.Header.InheritationLength
    );
    if (oserr != OS_EOK) {
        goto error;
    }

    g_rawCommandLine = arguments;
    g_baseLibraries = libraries;
    g_environment = environment;
    return OS_EOK;

error:
    free(arguments);
    free(libraries);
    __free_environment(environment);
    return oserr;
}

static oserr_t
__get_startup_info(void)
{
    OSHandle_t               mapping;
    SHMTransfer_t            transfer;
    struct sys_shm_transfer  destination;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    uuid_t                   processID = UUID_INVALID;
    uint32_t                 bytesWritten = 0;
    oserr_t                  oserr;

    oserr = __SHMTransferAllocate(
        PROCESS_STARTUP_MAX_SIZE,
        GrantTokenScopeRead | GrantTokenScopeWrite,
        &mapping,
        &transfer
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __ToSysSHMTransfer(&transfer, &destination);
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    if (sys_process_get_startup_information(
            GetGrachtClient(), &msg.base, ThreadsCurrentId(), &destination) != 0) {
        oserr = OS_EUNKNOWN;
        goto cleanup;
    }

    oserr = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, 0);
    if (oserr != OS_EOK) {
        goto cleanup;
    }

    if (sys_process_get_startup_information_result(
            GetGrachtClient(), &msg.base, &oserr, &processID, &bytesWritten) != 0) {
        oserr = OS_EPROTOCOL;
    }

cleanup:
    OSGrantTokenRevoke(transfer.Token.ControlHandleID);
    if (oserr == OS_EOK) {
        // If we retrieved startup information, parse it
        g_processId = processID;
        oserr = __parse_startup_info(&mapping, bytesWritten);
    }
    OSHandleDestroy(&mapping);
    return oserr;
}

static void
__setup_client(
    void)
{
    gracht_client_configuration_t clientConfig;
    int                           status;
    TRACE("__setup_client()");

    // initialite the ipc link
    status = gracht_link_vali_create(&g_gclientLink);
    if (status) {
        ERROR("__crt_process_initialize gracht_link_vali_create failed %i", status);
        _Exit(status);
    }

    // configure the client
    gracht_client_configuration_init(&clientConfig);
    gracht_client_configuration_set_link(&clientConfig, (struct gracht_link*)g_gclientLink);

    TRACE("__crt_process_initialize creating rpc client");
    status = gracht_client_create(&clientConfig, &g_gclient);
    if (status) {
        ERROR("__crt_process_initialize gracht_link_vali_client_create failed %i", status);
        _Exit(status);
    }

    TRACE("__crt_process_initialize connecting client");
    status = gracht_client_connect(g_gclient);
    if (status) {
        ERROR("__crt_process_initialize gracht_client_connect failed %i", status);
        _Exit(status);
    }

    // mark the client priority as we need the client for shutting
    // down during crt finalizing
    __mark_iod_priority(gracht_client_iod(g_gclient));
}

static void
__startup(
    _In_ void* argument,
    _In_ void* cancellationToken)
{
    bool*             complete = argument;
    OSAsyncContext_t* asyncContext;
    oserr_t           oserr;
    TRACE("__startup()");

    // Client setup and startup RPCs must run in a job, without asynchronous I/O.
    asyncContext = __tls_current()->async_context;
    __tls_current()->async_context = NULL;
    __setup_client();

    // Unless it's the phoenix bootstrapper, we retrieve startup information
    // and various process related configurations before proceeding.
    if (!g_isPhoenix) {
        oserr = __get_startup_info();
        if (oserr != OS_EOK) {
            ERROR("__startup failed to get process startup information: %u", oserr);
            (void)OsErrToErrNo(oserr);
            _Exit(-errno);
        }
    }

    // now that we have the startup information, we can proceed to setup systems
    // that require it.
    StdioConfigureStandardHandles();
    __tls_current()->async_context = asyncContext;
    *complete = true;
}

void
__crt_process_initialize(
    _In_ int isPhoenix)
{
    struct timespec deadline;
    bool            complete = false;
    int             status;
    TRACE("__crt_process_initialize(isPhoenix=%i)", isPhoenix);

    // We must mark whether this is the phoenix process
    // before any further initialization.
    g_isPhoenix = isPhoenix;

    // Get the handle of the startup thread, so we always know
    // the thread id of the primary process thread.
    g_startupThreadId = ThreadsCurrentId();

    // Initialize the standard C library
    TRACE("__crt_process_initialize initializing stdio");
    StdioInitialize();
    TRACE("__crt_process_initialize initializing stdsig");
    StdSignalInitialize();
    TRACE("__crt_process_initialize initializing compat");
    StdMmapInitialize();
    TRACE("__crt_process_initialize initializing oshandles");
    OSHandlesInitialize();

    // Initialize the userspace scheduler to support request based
    // asynchronous programs, and do this before we do any further
    // initialization, as it might require the usched scheduler to be working
    usched_xunit_init();

    if (usched_job_queue(__startup, &complete) == UUID_INVALID) {
        ERROR("__crt_process_initialize failed to queue startup");
        ThreadsFastExit(-ENOMEM);
    }

    // Constructors need startup data and inherited descriptors. No other
    // execution units exist yet, so the completion flag stays on this thread.
    while (!complete) {
        status = usched_yield(&deadline);
        if (complete) {
            break;
        }
        if (status == 0) {
            continue;
        }
        if (errno == EWOULDBLOCK) {
            usched_timedwait(&deadline);
        } else if (errno == ENOENT) {
            usched_wait();
        } else {
            status = errno;
            ERROR("__crt_process_initialize scheduler failed: %i", status);
            ThreadsFastExit(-status);
        }
    }

    // Refresh the initial thread, not the temporary bootstrap job.
    if (!g_isPhoenix && __tls_update_environment() != 0) {
        ERROR("__crt_process_initialize failed to update environment");
        ThreadsFastExit(-ENOMEM);
    }
}

int __crt_is_phoenix(void)
{
    return g_isPhoenix;
}

uuid_t __crt_primary_thread(void)
{
    return g_startupThreadId;
}

// This function returns the job id if set, otherwise it will return the
// real thread id.
uuid_t __crt_thread_id(void)
{
    struct thread_storage* tls = __tls_current();
    if (tls->job_id != UUID_INVALID) {
        return tls->job_id;
    }
    return ThreadsCurrentId();
}

uuid_t __crt_process_id(void)
{
    return g_processId;
}

const char* __crt_cmdline(void)
{
    return &g_rawCommandLine[0];
}

const char* const* __crt_environment(void)
{
    if (g_environment == NULL) {
        return g_nullEnvironment;
    }
    return (const char* const*)g_environment;
}

const uintptr_t* __crt_base_libraries(void)
{
    return g_baseLibraries;
}

gracht_client_t* GetGrachtClient(void)
{
    return g_gclient;
}
