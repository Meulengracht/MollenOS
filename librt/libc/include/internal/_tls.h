#ifndef __INTERNAL_TLS__
#define __INTERNAL_TLS__

#include <errno.h>
#include <os/osdefs.h>
#include <os/types/handle.h>
#include <os/types/async.h>
#include <stdio.h>
#include <wchar.h>

// Number of tls entries
#define TLS_NUMBER_ENTRIES 64

typedef struct thread_storage {
    uuid_t             thread_id;
    uuid_t             job_id;
    void*              handle;
    const char* const* env_block;
    errno_t            err_no;
    void*              locale;
    mbstate_t          mbst;
    unsigned int       seed;
    char*              strtok_next;
    struct tm          tm_buffer;
    char               asc_buffer[26];
    char               tmpname_buffer[L_tmpnam];
    OSHandle_t         shm;
    OSAsyncContext_t*  async_context;
    uintptr_t          tls_array[TLS_NUMBER_ENTRIES];
    // Number of registry slots (from index 0) already allocated for this
    // thread. __tls_prepare_modules() is incremental/idempotent: it only
    // allocates slots in [tls_modules_prepared_count, current registry size).
    unsigned int       tls_modules_prepared_count;
    // The list of thread-local destructors associated with this thread.
    void*              tls_destructors;
} thread_storage_t;

/**
 * @brief Initializes a new TLS instance
 * @param tls
 * @return
 */
CRTDECL(int, __tls_initialize(struct thread_storage* tls, int use));

/**
 * @brief Refreshes the current environment for the calling
 * thread. This is specifically designed to be called once the
 * startup information for the process has been retrieved.
 */
extern int __tls_update_environment(void);

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
 * @brief obtain an already initialized PE TLS module block for the
 * active logical thread. Ordinary C/AAPCS64 call;
 * Should not be marked const/pure, allocate, yield, or depend on implicit TLS.
 * Invalid/uninitialized indices are fatal runtime-initialization errors.
 */
CRTDECL(void*, __vali_tls_get_block(unsigned int moduleIndex));


/**
 * @brief Retrieves the local dma buffer for the current thread. Use this
 * function instead of accessing the dma buffer member manually as it is
 * allocated on demand.
 * @return The current dma buffer for the calling thread
 */
CRTDECL(OSHandle_t*, __tls_current_dmabuf(void));

// Startup-only module registry: sealed by the first preparation. No ID reuse.
CRTDECL(int, __tls_register_module(void* owner, const void* data, size_t size,
    size_t zero, size_t alignment, unsigned long* index));
CRTDECL(int, __tls_prepare_modules(void));
CRTDECL(void, __tls_release_modules(void));
CRTDECL(int, __tls_atexit(void (*function)(void*), void* argument, void* owner));
CRTDECL(void, __tls_run_destructors(void* owner));

#endif //!__INTERNAL_TLS__
