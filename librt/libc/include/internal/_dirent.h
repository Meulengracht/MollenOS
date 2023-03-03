#ifndef __INTERNAL_DIRENT_H__
#define __INTERNAL_DIRENT_H__

#include <os/types/handle.h>
#include <io.h>

typedef struct DIR {
    OSHandle_t    _handle;
    struct dirent _cdirent;
} DIR;

#endif //!__INTERNAL_DIRENT_H__
