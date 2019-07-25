#ifndef __INTERNAL_SOCKET_H__
#define __INTERNAL_SOCKET_H__

#include <ddk/ringbuffer.h>
#include <inet/socket.h>

typedef struct stdio_handle stdio_handle_t;
typedef intmax_t(*socket_recv)(stdio_handle_t*, struct msghdr*, int);
typedef intmax_t(*socket_send)(stdio_handle_t*, const struct msghdr*, int);

struct socket_ops {
    socket_recv recv;
    socket_send send;
};

struct socket {
    int                     domain;
    int                     type;
    int                     protocol;
    unsigned int            flags;
    struct sockaddr_storage default_address;
    
    // These below members must be fixed up on each inherit
    // as they are local pointers.
    struct socket_ops       domain_ops;
    ringbuffer_t*           recv_queue;
};

#define SOCKET_BOUND          0x00000001
#define SOCKET_CONNECTED      0x00000002
#define SOCKET_PASSIVE        0x00000004
#define SOCKET_WRITE_DISABLED 0x00000008
#define SOCKET_READ_DISABLED  0x00000010

// Local (vali) domain operations
void get_socket_ops_local(struct socket_ops*);
void get_socket_ops_inet(struct socket_ops*);
void get_socket_ops_null(struct socket_ops*);

#endif //!__INTERNAL_SOCKET_H__
