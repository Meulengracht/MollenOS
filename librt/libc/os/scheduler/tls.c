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

#include <ddk/ddkdefs.h> // for __reserved
#include <internal/_locale.h>
#include <internal/_utils.h>
#include <os/usched/usched.h>
#include <stdlib.h>
#include <string.h>
#include "private.h"

static const char* g_nullEnvironment[] = {
        NULL
};

static const char* const* __clone_env_block(void)
{
    const char* const* source = __crt_environment();
    char**             copy;
    int                count;

    if (source == NULL) {
        return (const char* const*)g_nullEnvironment;
    }

    count = 0;
    while (source[count]) {
        count++;
    }

    copy = calloc(count + 1, sizeof(char*));
    if (copy == NULL) {
        return (const char* const*)g_nullEnvironment;
    }

    count = 0;
    while (source[count]) {
        copy[count] = strdup(source[count]);
        count++;
    }
    return (const char* const*)copy;
}

int __usched_tls_init(struct thread_storage* tls)
{
    struct dma_buffer_info info;
    void*                  buffer;

    memset(tls, 0, sizeof(struct thread_storage));

    // Store it at reserved pointer place first
    __set_reserved(0, (size_t)tls);
    __set_reserved(1, (size_t)&tls->tls_array[0]);
    __set_reserved(11, (size_t)&tls->tls_array[0]);

    // Initialize members to default values
    tls->thr_id = UUID_INVALID;
    tls->err_no = EOK;
    tls->locale = __get_global_locale();
    tls->seed   = 1;

    // this may end up returning NULL environment for the primary thread if the
    // CRT hasn't fully initialized yet. Ignore it, and see what happens
    tls->env_block = __clone_env_block();

    // Setup a local transfer buffer for stdio operations
    // TODO: do on first read/write instead?
    buffer = malloc(BUFSIZ);
    if (buffer == NULL) {
        return OsOutOfMemory;
    }

    info.name     = "thread_tls";
    info.length   = BUFSIZ;
    info.capacity = BUFSIZ;
    info.flags    = DMA_PERSISTANT;
    info.type     = DMA_TYPE_DRIVER_32;
    return dma_export(buffer, &info, &tls->transfer_buffer);
}

static void __destroy_env_block(char** env)
{
    for (int i = 0; env[i] != NULL; i++) {
        free(env[i]);
    }
    free(env);
}

void __usched_tls_destroy(struct thread_storage* tls)
{
    // TODO: this is called twice for primary thread. Look into this
    if (tls->transfer_buffer.buffer != NULL) {
        dma_detach(&tls->transfer_buffer);
        free(tls->transfer_buffer.buffer);
        tls->transfer_buffer.buffer = NULL;
    }

    if (tls->env_block != NULL && tls->env_block != g_nullEnvironment) {
        __destroy_env_block((char**)tls->env_block);
        tls->env_block = NULL;
    }
}

struct thread_storage* usched_tls_current(void) {
    return (thread_storage_t*)__get_reserved(0);
}

void __usched_tls_switch(struct thread_storage* tls) {
    __set_reserved(0, (size_t)tls);
    __set_reserved(1, (size_t)&tls->tls_array[0]);
    __set_reserved(11, (size_t)&tls->tls_array[0]);
}
