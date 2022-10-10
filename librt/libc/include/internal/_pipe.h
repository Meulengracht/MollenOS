#ifndef __INTERNAL_PIPE_H__
#define __INTERNAL_PIPE_H__

#include <os/types/dma.h>

struct pipe {
    DMAAttachment_t attachment;
    unsigned int    options;
};

#endif //!__INTERNAL_PIPE_H__
