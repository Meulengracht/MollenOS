/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS C11-Support Threading Implementation
 * - Definitions, prototypes and information needed.
 */

#ifndef __STDC_TLS__
#define __STDC_TLS__

#include <errno.h>
#include <os/osdefs.h>
#include <os/dmabuf.h>
#include <stdio.h>
#include <threads.h>
#include <wchar.h>

// Number of tls entries
#define TLS_NUMBER_ENTRIES 64

PACKED_TYPESTRUCT(thread_storage, {
    thrd_t                thr_id;
    void*                 handle;
    errno_t               err_no;
    void*                 locale;
    mbstate_t             mbst;
    unsigned int          seed;
    char*                 strtok_next;
    struct tm             tm_buffer;
    char                  asc_buffer[26];
    char                  tmpname_buffer[L_tmpnam];
    struct dma_attachment transfer_buffer;
    uintptr_t             tls_array[TLS_NUMBER_ENTRIES];
});

_CODE_BEGIN
/* tls_current 
 * Retrieves the local storage space for the current thread */
CRTDECL(thread_storage_t*, tls_current(void));

/* tls_create
 * Initializes a new thread-storage space for the caller thread.
 * Part of CRT initialization routines. */
CRTDECL(OsStatus_t, tls_create(thread_storage_t *Tls));

/* tls_destroy
 * Destroys a thread-storage space should be called by thread crt */
CRTDECL(OsStatus_t, tls_destroy(thread_storage_t *Tls));

/* tls_cleanup
 * Destroys the TLS for the specific thread
 * by freeing resources and calling c11 destructors. */
CRTDECL(void, tls_cleanup(thrd_t thr, void* DsoHandle, int ExitCode));
CRTDECL(void, tls_cleanup_quick(thrd_t thr, void* DsoHandle, int ExitCode));
_CODE_END

#endif //!__STDC_TLS__
