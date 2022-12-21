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
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <internal/_io.h>
#include <internal/_utils.h>
#include <internal/_tls.h>
#include <os/threads.h>
#include <os/usched/xunit.h>
#include <os/usched/job.h>
#include <stdlib.h>
#include <string.h>

extern void StdioInitialize(void);
extern void StdioConfigureStandardHandles(void* inheritanceBlock);
extern void StdSignalInitialize(void);

// The default inbuilt client for rpc communication. In general this should only be used
// internally for calls to services and modules.
static gracht_client_t*         g_gclient     = NULL;
static struct gracht_link_vali* g_gclientLink = NULL;

static char*      g_rawCommandLine  = NULL;
static uint8_t*   g_inheritBlock    = NULL;
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
    stdio_handle_flag(handle, WX_PRIORITY);
}

static uint8_t* memdup(const char* data, size_t length)
{
    uint8_t* mem = malloc(length);
    if (mem == NULL) {
        return NULL;
    }
    memcpy(mem, data, length);
    return mem;
}

// environment data is a null terminated array of
// null terminated strings
static int __build_environment(const char* environment)
{
    const char* i         = environment;
    int         pairCount = 0;
    TRACE("__build_environment(env=%s)", environment);

    while (*i) {
        // skip towards next entry, and skip the terminating zero
        i += strlen(i) + 1;
        pairCount++;
    }

    g_environment = calloc((pairCount + 1), sizeof(char*));
    if (g_environment == NULL) {
        return -1;
    }

    i         = environment;
    pairCount = 0;
    while (*i) {
        g_environment[pairCount++] = strdup(i);
        i += strlen(i) + 1;
    }
    return 0;
}

static int __create_startup_buffer(DMABuffer_t* buffer, DMAAttachment_t* mapping)
{
    buffer->capacity = KB(64);
    buffer->length   = KB(64);
    buffer->name     = "startup-info";
    buffer->flags    = 0;
    buffer->type     = DMA_TYPE_REGULAR;
    return DmaCreate(buffer, mapping);
}

static uintptr_t* __parse_library_handles(const char* buffer, size_t length)
{
    size_t     entries = length / sizeof(uintptr_t);
    uintptr_t* libraries;
    TRACE("__parse_library_handles(length=%u)", length);

    if (!entries) {
        return NULL;
    }

    libraries = calloc(entries + 1, sizeof(uintptr_t));
    if (libraries == NULL) {
        return NULL;
    }

    for (int i = 0; i < (int)entries; i++) {
        libraries[i] = ((uintptr_t*)buffer)[i];
    }
    return libraries;
}

static int __parse_startup_info(DMAAttachment_t* dmaAttachment)
{
    ProcessStartupInformation_t* source;
    const char*                  data;
    TRACE("__parse_startup_info()");

    source = (ProcessStartupInformation_t*)dmaAttachment->buffer;
    data   = (char*)dmaAttachment->buffer + sizeof(ProcessStartupInformation_t);
    TRACE("[init] args-len %" PRIuIN ", inherit-len %" PRIuIN ", modules-len %" PRIuIN,
          source->ArgumentsLength,
          source->InheritationLength,
          source->LibraryEntriesLength);
    assert((source->ArgumentsLength + source->InheritationLength +
            source->LibraryEntriesLength + source->EnvironmentBlockLength)
           < KB(64));

    // Required - arguments (commandline) is written without zero terminator
    g_rawCommandLine = strndup(data, source->ArgumentsLength);
    if (g_rawCommandLine == NULL) {
        return -1;
    }
    data += source->ArgumentsLength;

    // Optional - inheritation block is not required and may be null
    if (source->InheritationLength) {
        g_inheritBlock = memdup(data, source->InheritationLength);
        data += source->InheritationLength;
    }

    // Required - the list of library entry points that needs to be called upon
    // CRT init/finit. This is the base library list, which are the libraries that
    // were automatically loaded during load of this process.
    g_baseLibraries = __parse_library_handles(data, source->LibraryEntriesLength);
    if (g_baseLibraries == NULL) {
        return -1;
    }
    data += source->LibraryEntriesLength;

    // Optional - environmental block. List of strings, does not need to be provided.
    if (source->EnvironmentBlockLength) {
        if (__build_environment(data)) {
            return -1;
        }
        data += source->EnvironmentBlockLength;
    }
    return 0;
}

static int __get_startup_info(void)
{
    DMABuffer_t              buffer;
    DMAAttachment_t          mapping;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    int                      status;
    oserr_t                  osStatus;
    TRACE("__get_startup_info()");

    status = __create_startup_buffer(&buffer, &mapping);
    if (status) {
        return status;
    }

    sys_process_get_startup_information(
            GetGrachtClient(),
            &msg.base,
            ThreadsCurrentId(),
            mapping.handle,
            0
    );
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_process_get_startup_information_result(
            GetGrachtClient(),
            &msg.base,
            &osStatus,
            &g_processId
    );
    assert(osStatus == OS_EOK);

    status = __parse_startup_info(&mapping);
    DmaAttachmentUnmap(&mapping);
    DmaDetach(&mapping);
    return status;
}

static void __setup_client(void* argument, void* cancellationToken)
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

static void __startup(void* argument, void* cancellationToken)
{
    TRACE("__startup()");

    // Unless it's the phoenix bootstrapper, we retrieve startup information
    // and various process related configurations before proceeding.
    if (!g_isPhoenix) {
        OSAsyncContext_t* asyncContext;
        int               status;

        // during startup, we disable async operations as we need the queued
        // jobs to run sequentially.
        asyncContext = __tls_current()->async_context;
        __tls_current()->async_context = NULL;

        status = __get_startup_info();
        if (status) {
            ERROR("__startup failed to get process startup information");
            _Exit(status);
        }

        // restore async context again
        __tls_current()->async_context = asyncContext;
    }

    // now that we have the startup information, we can proceed to setup systems
    // that require it.
    StdioConfigureStandardHandles(g_inheritBlock);
}

void __crt_process_initialize(int isPhoenix)
{
    TRACE("__crt_process_initialize(isPhoenix=%i)", isPhoenix);
    
    // We must set IsModule before anything
    g_isPhoenix = isPhoenix;

    // Get the handle of the startup thread, so we always know
    // the thread id of the primary process thread.
    g_startupThreadId = ThreadsCurrentId();

    // Initialize the standard C library
    TRACE("__crt_process_initialize initializing stdio");
    StdioInitialize();
    TRACE("__crt_process_initialize initializing stdsig");
    StdSignalInitialize();

    // Initialize the userspace scheduler to support request based
    // asynchronous programs, and do this before we do any further
    // initialization, as it might require the usched scheduler to be working
    usched_xunit_init();

    // Queue the client setup job, we need this before anything else. Jobs
    // run sequentially before we add more execution units.
    usched_job_queue(__setup_client, NULL);

    // Queue up the startup job which retrieves vital startup information, but
    // requires the gracht client to be running.
    usched_job_queue(__startup, NULL);
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
