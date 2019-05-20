#ifndef __INTERNAL_UTILS__
#define __INTERNAL_UTILS__

#include <os/osdefs.h>

typedef struct {
    _Atomic(int)* _futex0;
    _Atomic(int)* _futex1;
    int           _val0;
    int           _val1;
    int           _val2;
    int           _flags;
    size_t        _timeout;
} FutexParameters_t;

extern int IsProcessModule(void);
extern UUId_t* GetInternalProcessId(void);
extern const char* GetInternalCommandLine(void);

#endif