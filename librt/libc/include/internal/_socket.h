#ifndef __INTERNAL_SOCKET_H__
#define __INTERNAL_SOCKET_H__

#include <ddk/ringbuffer.h>
#include <inet/socket.h>

struct socket {
    int                     domain;
    int                     type;
    int                     protocol;
    unsigned int            flags;
    struct sockaddr_storage default_address;
    
    // Queues must be mapped into process space once they
    // are inherited. 
    ringbuffer_t* recv_queue;
    ringbuffer_t* send_queue;
};

#define SOCKET_BOUND          0x00000001
#define SOCKET_CONNECTED      0x00000002
#define SOCKET_PASSIVE        0x00000004
#define SOCKET_WRITE_DISABLED 0x00000008
#define SOCKET_READ_DISABLED  0x00000010

#endif //!__INTERNAL_SOCKET_H__
