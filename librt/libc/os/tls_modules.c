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
 *
 */

/** 
 * PE/COFF TLS module registry, shared by all architectures. Supports modules
 * registering after other threads are already running (dynamic loading);
 * see __tls_prepare_modules() for the per-thread catch-up contract.
 */
#include <internal/_tls.h>
#include <os/spinlock.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct __tls_module {
    // Dynamic shared object handle associated with this TLS module.
    void*       dso_handle;
    const void* data;
    size_t      size;
    size_t      zero;
    size_t      alignment;
};

// Append-only for the process lifetime: IDs are never recycled, so a module's
// _tls_index stays valid even after other modules register later (dynamic load).
static struct __tls_module g_modules[TLS_NUMBER_ENTRIES];
static unsigned int        g_count;
static spinlock_t          g_lock = _SPN_INITIALIZER_NP;

int __tls_register_module(
    _In_  void*          dsoHandle,
    _In_  const void*    data,
    _In_  size_t         size,
    _In_  size_t         zero,
    _In_  size_t         alignment,
    _Out_ unsigned long* index)
{
    unsigned int i;
    
    // Sanitize the module owner, and the index parameter, must always be provided.
    if (dsoHandle == NULL || index == NULL) {
        return -1;
    }

    // If size is set, template must be provided.
    if (size != 0 && data == NULL) {
        return -1;
    }

    // Validate the alignment, it must be a power of two and not exceed 8192 bytes.
    if (!alignment || (alignment & (alignment - 1)) || alignment > 8192) {
        return -1;
    }

    if (size > SIZE_MAX - zero || size + zero > SIZE_MAX - (alignment < sizeof(void*) ? sizeof(void*) : alignment) - sizeof(void*))
        return -1;
    
    spinlock_acquire(&g_lock);

    // check if the module is already registered
    for (i = 0; i < g_count; ++i) {
        if (g_modules[i].dso_handle == dsoHandle) {
            struct __tls_module* mod = &g_modules[i];
            int                  match;
            
            // Okay, so the dso handle matches, which means that we have
            // already registered it, so this should hopefully match
            match = mod->data == data && mod->size == size &&
                    mod->zero == zero && mod->alignment == alignment;
            if (match) {
                *index = i;
            }
            spinlock_release(&g_lock);
            return match ? 0 : -1;
        }
    }
    
    if (g_count == TLS_NUMBER_ENTRIES) {
        spinlock_release(&g_lock);
        return -1;
    }

    // all looks correct, we will store it!
    g_modules[g_count] = (struct __tls_module){
        dsoHandle,
        data,
        size,
        zero,
        alignment
    };

    // allocate a new global id for this module
    *index = g_count++;
    spinlock_release(&g_lock);
    return 0;
}

// Do not overwrite existing ownership on corrupted/reentrant preparation.
static int __verify_state(thread_storage_t* tls, unsigned int start, unsigned int count)
{
    if (start > count) {
        return -1;
    }
    
    for (unsigned int i = 0; i < start; ++i) {
        if (!tls->tls_array[i]) {
            return -1;
        }
    }
    
    for (unsigned int i = start; i < count; ++i) {
        if (tls->tls_array[i]) {
            return -1;
        }
    }
    return 0;
}

// Incremental and idempotent: catches the calling thread's tls_array up to
// whatever modules are currently registered, allocating only the slots it
// doesn't have yet. This is what makes it safe to call both at process/thread
// startup and again later, from DLL_ACTION_TLSREGISTER, when a module is
// loaded dynamically into an already-running process - already-prepared
// slots (and other threads' TLS) are left untouched.
int __tls_prepare_modules(void)
{
    thread_storage_t* tls = __tls_current();
    unsigned int      count;
    unsigned int      start;
    
    if (!tls) {
        return -1;
    }

    // Do a safe read of the global module count.
    spinlock_acquire(&g_lock);
    count = g_count;
    spinlock_release(&g_lock);

    start = tls->tls_modules_prepared_count;
    if (__verify_state(tls, start, count)) {
        return -1;
    }
    
    for (unsigned int i = start; i < count; ++i) {
        const struct __tls_module* mod = &g_modules[i];
        size_t                     alignment;
        size_t                     size;
        void*                      allocation;
        uintptr_t                  block;
        
        alignment = mod->alignment < sizeof(void*) ? sizeof(void*) : mod->alignment;
        size = mod->size + mod->zero;
        
        // Even empty modules have a stable, distinct allocation and registry ID.
        allocation = malloc((size ? size : 1) + alignment + sizeof(void*));
        if (!allocation) {
            // Roll back only the slots allocated by this call, leaving any
            // previously-prepared modules (from an earlier call) intact.
            for (unsigned int j = start; j < i; ++j) {
                free(((void**)tls->tls_array[j])[-1]);
                tls->tls_array[j] = 0;
            }
            return -1;
        }
        
        // align the allocation
        block = ((uintptr_t)allocation + sizeof(void*) + alignment - 1) & ~(alignment - 1);
        ((void**)block)[-1] = allocation;
        
        // if the module has initialized data, copy it
        if (mod->size) {
            memcpy((void*)block, mod->data, mod->size);
        }
        
        // if the module has zero-initialized data, set it to zero
        if (mod->zero) {
            memset((char*)block + mod->size, 0, mod->zero);
        }

        // store it
        tls->tls_array[i] = block;
    }
    
    tls->tls_modules_prepared_count = count;
    return 0;
}

void __tls_release_modules(void)
{
    thread_storage_t* tls = __tls_current();

    // shouldn't happen
    if (tls == NULL) {
        __builtin_trap();
    }

    // Callers must finish all callbacks before releasing any module block.
    for (unsigned int i = 0; i < TLS_NUMBER_ENTRIES; ++i) {
        if (tls->tls_array[i]) {
            free(((void**)tls->tls_array[i])[-1]);
            tls->tls_array[i] = 0;
        }
    }
    tls->tls_modules_prepared_count = 0;
}

struct __tls_destructor {
    struct __tls_destructor* next;
    void                  (*function)(void*);
    void*                  argument;
    void*                  dso_handle;
};

int __tls_atexit(void (*function)(void*), void* argument, void* dsoHandle)
{
    thread_storage_t*        tls = __tls_current();
    struct __tls_destructor* entry;
    
    // shouldn't happen
    if (tls == NULL) {
        __builtin_trap();
    }

    if (function == NULL) {
        return -1;
    }
    
    entry = malloc(sizeof(struct __tls_destructor));
    if (!entry) {
        return -1;
    }

    // initialize the new entry, we insert at the head
    // of the list
    entry->next = tls->tls_destructors;
    entry->function = function;
    entry->argument = argument;
    entry->dso_handle = dsoHandle;

    // link in
    tls->tls_destructors = entry;
    return 0;
}

void __tls_run_destructors(void* dsoHandle)
{
    thread_storage_t* tls = __tls_current();
    
    // shouldn't happen
    if (tls == NULL) {
        __builtin_trap();
    }

    for (;;) {
        // Re-read the head: callbacks can register additional destructors or
        // finalize another module. Never retain a link into a removed node.
        struct __tls_destructor** link = (struct __tls_destructor**)&tls->tls_destructors;
        struct __tls_destructor*  entry;

        // Skip over destructors that do not match the given DSO handle.
        while (*link && dsoHandle && (*link)->dso_handle != dsoHandle) {
            link = &(*link)->next;
        }

        // nothing found
        if (!*link) {
            return;
        }

        // Unlink the entry
        entry = *link;
        *link = entry->next;

        // Save the function and argument before freeing the entry.
        void (*function)(void*) = entry->function;
        void* argument = entry->argument;
        free(entry);
        
        // call the destructor
        function(argument);
    }
}
