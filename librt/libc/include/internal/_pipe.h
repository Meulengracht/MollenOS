#ifndef __INTERNAL_PIPE_H__
#define __INTERNAL_PIPE_H__

#include <os/types/shm.h>

struct pipe {
    SHMHandle_t  shm;
    unsigned int options;
};

#endif //!__INTERNAL_PIPE_H__
