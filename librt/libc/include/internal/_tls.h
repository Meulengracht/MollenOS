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

#endif //!__INTERNAL_TLS__
