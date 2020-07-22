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

extern int                IsProcessModule(void);
extern UUId_t*            GetInternalProcessId(void);
extern const char*        GetInternalCommandLine(void);

CRTDECL(gracht_client_t*, GetGrachtClient(void));
CRTDECL(void*, GetGrachtBuffer(void));
CRTDECL(UUId_t,           GetNativeHandle(int));

#endif