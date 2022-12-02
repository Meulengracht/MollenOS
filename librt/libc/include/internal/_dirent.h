#ifndef __INTERNAL_DIRENT_H__
#define __INTERNAL_DIRENT_H__

#include <os/osdefs.h>
#include <io.h>

typedef struct DIR {
    uuid_t        _handle;
    struct dirent _cdirent;
} DIR;

#endif //!__INTERNAL_DIRENT_H__
