#ifndef __INTERNAL_IPC_H__
#define __INTERNAL_IPC_H__

#include <ds/streambuffer.h>
#include <os/dmabuf.h>

struct ipcontext {
    streambuffer_t* stream;
};

#endif //!__INTERNAL_IPC_H__
