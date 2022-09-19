#ifndef __INTERNAL_TLS__
#define __INTERNAL_TLS__

#include <errno.h>
#include <os/osdefs.h>
#include <os/dmabuf.h>
#include <stdio.h>
#include <wchar.h>

// Number of tls entries
#define TLS_NUMBER_ENTRIES 64

typedef struct thread_storage {
    uuid_t                thr_id;
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
    struct dma_attachment dma;
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

/**
 * @brief Retrieves the local dma buffer for the current thread. Use this
 * function instead of accessing the dma buffer member manually as it is
 * allocated on demand.
 * @return The current dma buffer for the calling thread
 */
CRTDECL(struct dma_attachment*, __tls_current_dmabuf(void));

#endif //!__INTERNAL_TLS__
