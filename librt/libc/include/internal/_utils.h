#ifndef __INTERNAL_UTILS__
#define __INTERNAL_UTILS__

#include <os/osdefs.h>

typedef struct gracht_client gracht_client_t;

typedef struct FutexParameters {
    _Atomic(int)* _futex0;
    _Atomic(int)* _futex1;
    int           _val0;
    int           _val1;
    int           _val2;
    int           _flags;
    size_t        _timeout;
} FutexParameters_t;

typedef struct HandleSetWaitParameters {
    struct ioset_event* events;
    int                 maxEvents;
    size_t              timeout;
    int                 pollEvents;
} HandleSetWaitParameters_t;

extern int                __crt_is_phoenix(void);
extern uuid_t*            __crt_processid_ptr(void);
extern uuid_t             __crt_primary_thread(void);
extern const char*        __crt_cmdline(void);
extern const char* const* __crt_environment(void);
CRTDECL(const uintptr_t*, __crt_base_libraries(void));

CRTDECL(gracht_client_t*, GetGrachtClient(void));
CRTDECL(uuid_t,           GetNativeHandle(int));

#endif