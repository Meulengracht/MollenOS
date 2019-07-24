#ifndef __INTERNAL_SOCKET_H__
#define __INTERNAL_SOCKET_H__

#include <inet/socket.h>

enum socket_state {
    socket_idle,
    socket_bound,
    socket_listener,
    socket_connected
};

struct socket {
    int                     domain;
    int                     type;
    int                     protocol;
    int                     state;
    unsigned int            flags;
    void*                   recv_queue;
    struct sockaddr_storage default_address;
};

#define SOCKET_WRITE_DISABLED 0x1
#define SOCKET_READ_DISABLED  0x2

#endif //!__INTERNAL_SOCKET_H__
