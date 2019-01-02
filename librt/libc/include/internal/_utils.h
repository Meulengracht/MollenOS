#ifndef __INTERNAL_UTILS__
#define __INTERNAL_UTILS__

#include <os/osdefs.h>

extern int IsProcessModule(void);
extern UUId_t* GetInternalProcessId(void);
extern const char* GetInternalCommandLine(void);

#endif