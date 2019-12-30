#ifndef __INTERNAL_SOCKET_H__
#define __INTERNAL_SOCKET_H__

#include <ddk/streambuffer.h>
#include <inet/socket.h>
#include <os/dmabuf.h>

struct socket {
    int                     domain;
    int                     type;
    int                     protocol;
    struct sockaddr_storage default_address;
    struct dma_attachment   send_buffer;
    struct dma_attachment   recv_buffer;
};

struct packethdr {
    int      flags;
    intmax_t controllen;
    intmax_t addresslen;
    intmax_t payloadlen;
};

extern int socket_create(int, int, int, UUId_t, UUId_t, UUId_t);

#endif //!__INTERNAL_SOCKET_H__
