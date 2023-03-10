#ifndef __INTERNAL_UTILS__
#define __INTERNAL_UTILS__

#include <os/osdefs.h>

typedef struct gracht_client gracht_client_t;

extern int                __crt_is_phoenix(void);
extern uuid_t             __crt_process_id(void);
extern uuid_t             __crt_primary_thread(void);
extern uuid_t             __crt_thread_id(void);
extern const char*        __crt_cmdline(void);
extern const char* const* __crt_environment(void);
CRTDECL(const uintptr_t*, __crt_base_libraries(void));

CRTDECL(gracht_client_t*, GetGrachtClient(void));
CRTDECL(uuid_t,           GetNativeHandle(int));

#endif
