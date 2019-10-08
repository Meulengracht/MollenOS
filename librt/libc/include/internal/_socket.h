#ifndef __INTERNAL_SOCKET_H__
#define __INTERNAL_SOCKET_H__

#include <ddk/streambuffer.h>
#include <inet/socket.h>
#include <os/dmabuf.h>

struct socket {
    int                     domain;
    int                     type;
    int                     protocol;
    unsigned int            flags;
    struct sockaddr_storage default_address;
    
    struct dma_attachment send_buffer;
    struct dma_attachment recv_buffer;
};

struct packethdr {
    int      flags;
    intmax_t controllen;
    intmax_t addresslen;
    intmax_t payloadlen;
};

#define SOCKET_BOUND          0x00000001
#define SOCKET_CONNECTED      0x00000002
#define SOCKET_PASSIVE        0x00000004
#define SOCKET_WRITE_DISABLED 0x00000008
#define SOCKET_READ_DISABLED  0x00000010

#endif //!__INTERNAL_SOCKET_H__
