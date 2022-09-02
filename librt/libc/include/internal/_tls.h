#ifndef __INTERNAL_TLS__
#define __INTERNAL_TLS__

#include <errno.h>
#include <os/osdefs.h>
#include <os/dmabuf.h>
#include <stdio.h>
#include <threads.h>
#include <wchar.h>

// Number of tls entries
#define TLS_NUMBER_ENTRIES 64

typedef struct thread_storage {
    thrd_t                thr_id;
    void*                 handle;
    const char* const*    env_block;
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
} thread_storage_t;

/**
 * @brief Initializes a new TLS instance
 * @param tls
 * @return
 */
CRTDECL(int,  __tls_initialize(struct thread_storage* tls));

/**
 * @brief
 * @param tls
 */
CRTDECL(void, __tls_switch(struct thread_storage* tls));

/**
 * @brief
 * @param tls
 */
CRTDECL(void, __tls_destroy(struct thread_storage* tls));

/**
 * @brief Retrieves the local storage space for the current thread
 * @return The current TLS structure for the calling thread
 */
CRTDECL(struct thread_storage*, __tls_current(void));

#endif //!__INTERNAL_TLS__
