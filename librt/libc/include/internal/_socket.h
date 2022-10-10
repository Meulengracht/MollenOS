#ifndef __INTERNAL_SOCKET_H__
#define __INTERNAL_SOCKET_H__

#include <ds/streambuffer.h>
#include <inet/socket.h>
#include <os/types/dma.h>

struct socket {
    int                     domain;
    int                     type;
    int                     protocol;
    struct sockaddr_storage default_address;
    DMAAttachment_t         send_buffer;
    DMAAttachment_t         recv_buffer;
};

struct packethdr {
    int      flags;
    intmax_t controllen;
    intmax_t addresslen;
    intmax_t payloadlen;
};

extern int socket_create(int, int, int, uuid_t, uuid_t, uuid_t);

#endif //!__INTERNAL_SOCKET_H__
