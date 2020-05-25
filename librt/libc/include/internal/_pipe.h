#ifndef __INTERNAL_PIPE_H__
#define __INTERNAL_PIPE_H__

#include <os/dmabuf.h>

struct pipe {
    struct dma_attachment attachment;
    unsigned int          options;
};

#endif //!__INTERNAL_PIPE_H__
