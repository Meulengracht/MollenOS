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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Process Flow:
 *  - Startup:          __crt_tls_create(main_thread)
 *  - Cleanup (Normal)  __cxa_exithandlers, tss_cleanup(thread/process), tls_destroy
 *  - Cleanup (Quick)   __cxa_exithandlers, tss_cleanup_quick(thread/process), tls_destroy
 *
 * Thread Flow:
 *  - Startup:          __crt_tls_create(new_thread), __cxa_threadinitialize
 *  - Cleanup:          tss_cleanup(thread), tls_destroy, __cxa_threadfinalize
 */

#ifndef LIBC_KERNEL
#include <ds/hashtable.h>
#include <ds/list.h>
#include <internal/_ipc.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/spinlock.h>
#include <signal.h>
#include <stdlib.h>
#include "../threads/tss.h"

/* exit
 * Causes normal program termination to occur.
 * Several cleanup steps are performed:
 *  - functions passed to atexit are called, in reverse order of registration all C streams are flushed and closed
 *  - files created by tmpfile are removed
 *  - control is returned to the host environment. If exit_code is zero or EXIT_SUCCESS, 
 *    an implementation-defined status, indicating successful termination is returned. 
 *    If exit_code is EXIT_FAILURE, an implementation-defined status, indicating unsuccessful 
 *    termination is returned. In other cases implementation-defined status value is returned. */
extern void  __cxa_exithandlers(void);
extern int   __cxa_at_quick_exit(void (*fn)(void*), void* dsoHandle);
extern int   __cxa_atexit(void (*fn)(void*), void* argument, void* dsoHandle);
extern void  __cxa_threadfinalize(void);
extern void  StdioCleanup(void);
extern void* __dso_handle;

struct atexit_handler_entry {
    element_t header;
    void* argument;
    union {
        void     (*atexit_fn)(void*, int);
        tss_dtor_t atexit_dctor;
    } callback;
};

struct atexit_dso_entry {
    void*  dso_handle;
    list_t values;
};

static uint64_t atexit_dso_hash(const void* element);
static int      atexit_dso_cmp(const void* element1, const void* element2);

struct atexit_thread_entry {
    thrd_t      thread_id;
    hashtable_t values;
};

static uint64_t atexit_thread_hash(const void* element);
static int      atexit_thread_cmp(const void* element1, const void* element2);

struct at_exit_manager {
    int        initialized;
    int        ran;
    spinlock_t lock;

    // Contains a hashtable of hashtables, key = thread, value = hashtable
    hashtable_t handlers;
};

static struct at_exit_manager g_at_exit       = { 0, 0, _SPN_INITIALIZER_NP(spinlock_plain), { 0 } };
static struct at_exit_manager g_at_quick_exit = { 0, 0, _SPN_INITIALIZER_NP(spinlock_plain), { 0 } };
static int         g_exit_in_progress   = 0;
static int         g_exit_code          = 0;
static spinlock_t  g_exit_lock          = _SPN_INITIALIZER_NP(spinlock_plain);

static int __initialize_handlers_level_3(
        _In_ struct atexit_dso_entry* entry,
        _In_ void*                    dsoHandle)
{
    entry->dso_handle = dsoHandle;
    list_construct(&entry->values);
}

static int __initialize_handlers_level_2(
        _In_ struct atexit_thread_entry* entry,
        _In_ thrd_t                      threadID)
{
    entry->thread_id = threadID;
    hashtable_construct(
            &entry->values,
            0,
            sizeof(struct atexit_dso_entry),
            atexit_dso_hash,
            atexit_dso_cmp
    );
}

static int __initialize_handlers_level_1(
        _In_ struct at_exit_manager* manager)
{
    hashtable_construct(
            &manager->handlers,
            0,
            sizeof(struct atexit_thread_entry),
            atexit_thread_hash,
            atexit_thread_cmp
    );
}

static struct atexit_thread_entry* __get_or_insert_thread(
        _In_ struct at_exit_manager* manager,
        _In_ thrd_t                  threadID)
{
    struct atexit_thread_entry* entry;

    do {
        entry = hashtable_get(&manager->handlers, &(struct atexit_thread_entry) {
                .thread_id = threadID
        });
        if (entry == NULL) {
            struct atexit_thread_entry e;
            __initialize_handlers_level_2(&e, threadID);
            hashtable_set(&manager->handlers, &e);
        }
    } while (entry == NULL);
    return NULL;
}

static struct atexit_dso_entry* __get_or_insert_dso(
        _In_ struct atexit_thread_entry* threadEntry,
        _In_ void*                       dsoHandle)
{
    struct atexit_dso_entry* entry;

    do {
        entry = hashtable_get(&threadEntry->values, &(struct atexit_dso_entry) {
                .dso_handle = dsoHandle
        });
        if (entry == NULL) {
            struct atexit_dso_entry e;
            __initialize_handlers_level_3(&e, dsoHandle);
            hashtable_set(&threadEntry->values, &e);
        }
    } while (entry == NULL);
    return NULL;
}

static void __register_handler(
        _In_ struct at_exit_manager* manager,
        _In_ thrd_t                  threadID,
        _In_ void                    (*atExitFn)(void*),
        _In_ void*                   argument,
        _In_ void*                   dsoHandle)
{
    struct atexit_thread_entry*  threadEntry;
    struct atexit_dso_entry*     dsoEntry;
    struct atexit_handler_entry* handlerEntry;

    threadEntry = __get_or_insert_thread(manager, threadID);
    assert(threadEntry != NULL);

    dsoEntry = __get_or_insert_dso(threadEntry, dsoHandle);
    assert(dsoHandle != NULL);

    handlerEntry = malloc(sizeof(struct atexit_handler_entry));
    assert(handlerEntry != NULL);

    ELEMENT_INIT(&handlerEntry->header, 0, 0);
    handlerEntry->argument = argument;
    if (threadID != UUID_INVALID) {
        handlerEntry->callback.atexit_dctor = atExitFn;
    } else {
        handlerEntry->callback.atexit_fn = (void(*)(void*, int))atExitFn;
    }
    list_append(&dsoEntry->values, &handlerEntry->header);
}

void __at_exit_impl(
        _In_ thrd_t threadID,
        _In_ void   (*atExitFn)(void*),
        _In_ void*  argument,
        _In_ void*  dsoHandle)
{
    spinlock_acquire(&g_at_exit.lock);

    // Do not register any handlers once we've run primary cleanup
    // to avoid any expectations.
    if (g_at_exit.ran) {
        spinlock_release(&g_at_exit.lock);
        return;
    }

    if (!g_at_exit.initialized) {
        assert(__initialize_handlers_level_1(&g_at_exit) == 0);
        g_at_exit.initialized = 1;
    }

    __register_handler(&g_at_exit, threadID, atExitFn, argument, dsoHandle);
    spinlock_release(&g_at_exit.lock);
}

void __at_quick_exit_impl(
        _In_ thrd_t threadID,
        _In_ void   (*atExitFn)(void*),
        _In_ void*  argument,
        _In_ void*  dsoHandle)
{
    spinlock_acquire(&g_at_quick_exit.lock);

    // Do not register any handlers once we've run primary cleanup
    // to avoid any expectations.
    if (g_at_quick_exit.ran) {
        spinlock_release(&g_at_quick_exit.lock);
        return;
    }

    if (!g_at_quick_exit.initialized) {
        assert(__initialize_handlers_level_1(&g_at_quick_exit) == 0);
        g_at_quick_exit.initialized = 1;
    }

    __register_handler(&g_at_quick_exit, threadID, atExitFn, argument, dsoHandle);
    spinlock_release(&g_at_quick_exit.lock);
}

struct __dso_iterate_context {
    thrd_t thread_id;
    int    exit_code;
};

static void
__run_and_clean(
        _In_ element_t* header,
        _In_ void*      userContext)
{
    struct atexit_handler_entry*  handler = (struct atexit_handler_entry*)header;
    struct __dso_iterate_context* context = userContext;
    if (context->thread_id == UUID_INVALID) {
        handler->callback.atexit_fn(handler->argument, context->exit_code);
    } else {
        handler->callback.atexit_dctor(handler->argument);
    }
}

static void
__run_dso_handlers(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    const struct atexit_dso_entry* dsoEntry = element;
    _CRT_UNUSED(index);
    list_clear((list_t*)&dsoEntry->values, __run_and_clean, userContext);
}

void __at_exit_run(
        _In_ struct at_exit_manager* manager,
        _In_ thrd_t                  threadID,
        _In_ void*                   dsoHandle,
        _In_ int                     exitCode)
{
    struct atexit_thread_entry*  threadEntry;
    struct __dso_iterate_context context;

    spinlock_acquire(&manager->lock);
    if (threadID == UUID_INVALID && dsoHandle == NULL) {
        manager->ran = 1;
    }

    threadEntry = hashtable_get(&manager->handlers, &(struct atexit_thread_entry) {
        .thread_id = threadID
    });
    if (threadEntry == NULL) {
        goto done;
    }

    context.thread_id = threadID;
    context.exit_code = exitCode;

    // If dsoHandle is NULL, we run all of them
    if (dsoHandle == NULL) {
        hashtable_enumerate(
                &threadEntry->values,
                __run_dso_handlers,
                &exitCode
        );
    } else {
        struct atexit_dso_entry* dsoEntry;
        dsoEntry = hashtable_get(&threadEntry->values, &(struct atexit_dso_entry) {
            .dso_handle = dsoHandle
        });
        if (dsoEntry != NULL) {
            list_clear((list_t*)&dsoEntry->values, __run_and_clean, &context);
        }
    }

done:
    spinlock_release(&manager->lock);
}

int at_quick_exit(void(*fn)(void)) {
    return __cxa_at_quick_exit((void (*)(void*))fn, __dso_handle);
}

int atexit(void (*fn)(void)) {
    return __cxa_atexit((void (*)(void*))fn, NULL, __dso_handle);
}

void exit(int exitCode)
{
    int ec = exitCode;

    spinlock_acquire(&g_exit_lock);
    if (g_exit_in_progress) {
        // Exit is already in progress, two cases can happen here, either
        // it's the primary thread, which needs to do the primary cleanup, or
        // it's another child thread crowing our exit
        if (thrd_current() != __crt_primary_thread()) {
            spinlock_release(&g_exit_lock);
            thrd_exit(g_exit_code);
        }
        ec = g_exit_code;
    } else {
        g_exit_in_progress = 1;
        g_exit_code        = exitCode;
    }
    spinlock_release(&g_exit_lock);

    // Are we not the primary thread? Then run cleanup for this thread, and interrupt
    // the primary thread, telling it to quit its job :-)
    if (thrd_current() != __crt_primary_thread()) {
        thrd_signal(__crt_primary_thread(), SIGEXIT);
        thrd_exit(EXIT_SUCCESS);
    }

    // important here that we use the gracht client BEFORE cleaning up the entire C runtime
    if (!__crt_is_phoenix()) {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        oserr_t                  oserr;
        sys_process_terminate(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), ec);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_process_terminate_result(GetGrachtClient(), &msg.base, &oserr);
    }

    // Otherwise, we are the main thread, which means we will go ahead and do primary
    // program cleanup, the moment we get killed, the rest of threads will be aborted
    __at_exit_run(&g_at_exit, UUID_INVALID, NULL, ec);
    __cxa_exithandlers();

    // Cleanup the c-library stuff last, this includes closing opened io descriptors,
    // flushing buffers etc.
    StdioCleanup();

    // Exit the primary thread
    Syscall_ThreadExit(ec);
    for(;;);
}

_Noreturn static void __thrd_quick_exit(
        _In_ int exitCode)
{
    __cxa_threadfinalize();
    Syscall_ThreadExit(exitCode);
    for(;;);
}

void quick_exit(int exitCode)
{
    int ec = exitCode;

    spinlock_acquire(&g_exit_lock);
    if (g_exit_in_progress) {
        // Exit is already in progress, two cases can happen here, either
        // it's the primary thread, which needs to do the primary cleanup, or
        // it's another child thread crowing our exit
        if (thrd_current() != __crt_primary_thread()) {
            spinlock_release(&g_exit_lock);
            __thrd_quick_exit(g_exit_code);
        }
        ec = g_exit_code;
    } else {
        g_exit_in_progress = 1;
        g_exit_code        = exitCode;
    }
    spinlock_release(&g_exit_lock);

    // Are we not the primary thread? Then run cleanup for this thread, and interrupt
    // the primary thread, telling it to quit its job :-)
    if (thrd_current() != __crt_primary_thread()) {
        thrd_signal(__crt_primary_thread(), SIGEXITQ);
        __thrd_quick_exit(EXIT_SUCCESS);
    }

    if (!__crt_is_phoenix()) {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        oserr_t               status;
        sys_process_terminate(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), exitCode);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_process_terminate_result(GetGrachtClient(), &msg.base, &status);
    }

    // Exit the primary thread
    Syscall_ThreadExit(ec);
    for(;;);
}

void
_Exit(
        _In_ int exitCode)
{
    int ec = exitCode;

    spinlock_acquire(&g_exit_lock);
    if (g_exit_in_progress) {
        // Exit is already in progress, two cases can happen here, either
        // it's the primary thread, which needs to do the primary cleanup, or
        // it's another child thread crowing our exit
        if (thrd_current() != __crt_primary_thread()) {
            spinlock_release(&g_exit_lock);
            Syscall_ThreadExit(g_exit_code);
            for(;;);
        }
        ec = g_exit_code;
    } else {
        g_exit_in_progress = 1;
        g_exit_code        = exitCode;
    }
    spinlock_release(&g_exit_lock);

    // Are we not the primary thread? Then run cleanup for this thread, and interrupt
    // the primary thread, telling it to quit its job :-)
    if (thrd_current() != __crt_primary_thread()) {
        thrd_signal(__crt_primary_thread(), SIGQUIT);
        Syscall_ThreadExit(ec);
        for(;;);
    }

    if (!__crt_is_phoenix()) {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        oserr_t               status;
        sys_process_terminate(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), exitCode);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_process_terminate_result(GetGrachtClient(), &msg.base, &status);
    }

    Syscall_ThreadExit(ec);
    for(;;);
}

uint64_t atexit_dso_hash(const void* element)
{
    const struct atexit_dso_entry* entry = element;
    return (uint64_t)(uintptr_t)entry->dso_handle;
}

int atexit_dso_cmp(const void* element1, const void* element2)
{
    const struct atexit_dso_entry* entry1 = element1;
    const struct atexit_dso_entry* entry2 = element2;
    return entry1->dso_handle == entry2->dso_handle ? 0 : -1;
}

uint64_t atexit_thread_hash(const void* element)
{
    const struct atexit_thread_entry* entry = element;
    return (uint64_t)entry->thread_id;
}

int atexit_thread_cmp(const void* element1, const void* element2)
{
    const struct atexit_thread_entry* entry1 = element1;
    const struct atexit_thread_entry* entry2 = element2;
    return entry1->thread_id == entry2->thread_id ? 0 : -1;
}

#endif //!LIBC_KERNEL
