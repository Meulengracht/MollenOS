/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Socket Support Definitions & Structures
 * - This header describes the base socket-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __INET_SOCKET_H__
#define	__INET_SOCKET_H__

#include <os/osdefs.h>

#ifndef	_SOCKLEN_T_DEFINED_
#define	_SOCKLEN_T_DEFINED_
typedef	__int_least32_t	socklen_t;
#endif

#ifndef	_SA_FAMILY_T_DEFINED_
#define	_SA_FAMILY_T_DEFINED_
typedef	uint16_t sa_family_t;
#endif

// Socket types
#define	SOCK_STREAM	    1   /* stream socket */
#define	SOCK_DGRAM      2   /* datagram socket */
#define	SOCK_RAW        3   /* datagram like raw-protocol interface */
#define	SOCK_RDM	    4   /* reliably-delivered message */
#define	SOCK_SEQPACKET	5	/* Connection based sequenced packet stream */

// Option flags per-socket.
#define	SO_DEBUG        0x0001  /* turn on debugging info recording */
#define	SO_ACCEPTCONN	0x0002  /* socket has had listen() */
#define	SO_REUSEADDR	0x0004  /* allow local address reuse */
#define	SO_KEEPALIVE	0x0008  /* keep connections alive */
#define	SO_DONTROUTE	0x0010  /* just use interface addresses */
#define	SO_BROADCAST	0x0020  /* permit sending of broadcast msgs */
#define	SO_USELOOPBACK	0x0040  /* bypass hardware when possible */
#define	SO_LINGER       0x0080  /* linger on close if data present */
#define	SO_OOBINLINE	0x0100  /* leave received OOB data in line */
#define	SO_REUSEPORT	0x0200  /* allow local address & port reuse */
#define SO_TIMESTAMP	0x0800  /* timestamp received dgram traffic */
#define SO_BINDANY      0x1000  /* allow bind to any address */
#define SO_ZEROIZE      0x2000  /* zero out all mbufs sent over socket */

//Additional options, not kept in so_options.
#define	SO_SNDBUF	0x1001  /* send buffer size */
#define	SO_RCVBUF	0x1002  /* receive buffer size */
#define	SO_SNDLOWAT	0x1003  /* send low-water mark */
#define	SO_RCVLOWAT	0x1004  /* receive low-water mark */
#define	SO_SNDTIMEO	0x1005  /* send timeout */
#define	SO_RCVTIMEO	0x1006  /* receive timeout */
#define	SO_ERROR	0x1007  /* get error status and clear */
#define	SO_TYPE		0x1008  /* get socket type */
#define	SO_NETPROC	0x1020  /* multiplex; network processing */
#define	SO_RTABLE	0x1021  /* routing table to be used */
#define	SO_PEERCRED	0x1022  /* get connect-time credentials */
#define	SO_SPLICE	0x1023  /* splice data to other socket */

// Structure used for manipulating linger option.
struct	linger {
    int	l_onoff;		/* option on/off */
    int	l_linger;		/* linger time */
};

// Level number for (get/set)sockopt() to apply to socket itself.
#define	SOL_SOCKET	0xffff

//Address families.
#define	AF_UNSPEC		0		/* unspecified */
#define	AF_UNIX			1		/* local to host */
#define	AF_LOCAL		AF_UNIX /* only to keep unix compatability */
#define	AF_INET			2		/* internetwork: UDP, TCP, etc. */
#define	AF_INET6		3		/* IPv6 */
#define	AF_BLUETOOTH	4		/* Bluetooth */
#define AF_MAX          5

// Structure used to store most addresses.
struct sockaddr {
    uint8_t     sa_len;		/* total length */
    sa_family_t sa_family;  /* address family */
    char        sa_data[14];/* actually longer; address value */
};

// Sockaddr type which can hold any sockaddr type available
// in the system.
// Note: __ss_{len,family} is defined in RFC2553.  During RFC2553 discussion
// the field name went back and forth between ss_len and __ss_len,
// and RFC2553 specifies it to be __ss_len.
struct sockaddr_storage {
    uint8_t       __ss_len;		    /* total length */
    sa_family_t   __ss_family;	    /* address family */
    unsigned char __ss_pad1[6];	    /* align to quad */
    uint64_t      __ss_pad2;	    /* force alignment for stupid compilers */
    unsigned char __ss_pad3[240];	/* pad to a total of 256 bytes */
};

// Protocol families, same as address families for now.
#define	PF_UNSPEC       AF_UNSPEC
#define	PF_LOCAL	    AF_LOCAL
#define	PF_UNIX		    AF_UNIX
#define	PF_INET		    AF_INET
#define PF_INET6	    AF_INET6
#define	PF_BLUETOOTH	AF_BLUETOOTH
#define	PF_MAX		    AF_MAX

// These are the valid values for the "how" field used by shutdown(2).
#define	SHUT_RD		0x1
#define	SHUT_WR		0x2
#define	SHUT_RDWR	(SHUT_RD | SHUT_WR)

// Maximum queue length specifiable by listen(2).
#define	SOMAXCONN	128

struct iovec {
   void*  iov_base;    /* Starting address */
   size_t iov_len;     /* Number of bytes to transfer */
};

// Message header for recvmsg and sendmsg calls.
// Used value-result for recvmsg, value only for sendmsg.
struct msghdr {
    void*         msg_name;         /* optional address */
    socklen_t     msg_namelen;      /* size of address */
    struct iovec* msg_iov;          /* scatter/gather array */
    unsigned int  msg_iovlen;       /* # elements in msg_iov */
    void*         msg_control;      /* ancillary data, see below */
    socklen_t     msg_controllen;   /* ancillary data buffer len */
    int           msg_flags;        /* flags on received message */
};

#define	MSG_OOB			    0x1	    /* process out-of-band data */
#define	MSG_PEEK		    0x2	    /* peek at incoming message */
#define	MSG_DONTROUTE		0x4	    /* send without using routing tables */
#define	MSG_EOR			    0x8	    /* data completes record */
#define	MSG_TRUNC		    0x10	/* data discarded before delivery */
#define	MSG_CTRUNC		    0x20	/* control data lost before delivery */
#define	MSG_WAITALL		    0x40	/* wait for full request or error */
#define	MSG_DONTWAIT		0x80	/* this message should be nonblocking */
#define	MSG_BCAST		    0x100	/* this message rec'd as broadcast */
#define	MSG_MCAST		    0x200	/* this message rec'd as multicast */
#define	MSG_NOSIGNAL		0x400	/* do not send SIGPIPE */
#define	MSG_CMSG_CLOEXEC	0x800	/* set FD_CLOEXEC on received fds */

// Header for ancillary data objects in msg_control buffer.
// Used for additional information with/about a datagram
// not expressible by flags.  The format is a sequence
// of message elements headed by cmsghdr structures.
struct cmsghdr {
    socklen_t cmsg_len;     /* data byte count, including hdr */
    int       cmsg_level;   /* originating protocol */
    int       cmsg_type;    /* protocol-specific type */
/* followed by	u_char  cmsg_data[]; */
};

// Given pointer to struct cmsghdr, return pointer to data
#define	CMSG_DATA(cmsg) \
    ((unsigned char *)(cmsg) + _ALIGN(sizeof(struct cmsghdr)))

// Given pointer to struct cmsghdr, return pointer to next cmsghdr
#define	CMSG_NXTHDR(mhdr, cmsg)	\
    (((char *)(cmsg) + _ALIGN((cmsg)->cmsg_len) + \
                _ALIGN(sizeof(struct cmsghdr)) > \
        ((char *)(mhdr)->msg_control) + (mhdr)->msg_controllen) ? \
        (struct cmsghdr *)NULL : \
        (struct cmsghdr *)((char *)(cmsg) + _ALIGN((cmsg)->cmsg_len)))

// RFC 2292 requires to check msg_controllen, in case that the kernel returns
// an empty list for some reasons.
#define	CMSG_FIRSTHDR(mhdr) \
    ((mhdr)->msg_controllen >= sizeof(struct cmsghdr) ? \
     (struct cmsghdr *)(mhdr)->msg_control : \
     (struct cmsghdr *)NULL)

// Length of the contents of a control message of length len
#define	CMSG_LEN(len)	(_ALIGN(sizeof(struct cmsghdr)) + (len))

// Length of the space taken up by a padded control message of length len
#define	CMSG_SPACE(len)	(_ALIGN(sizeof(struct cmsghdr)) + _ALIGN(len))

_CODE_BEGIN
CRTDECL(int,      socket(int, int, int));
CRTDECL(int,      socketpair(int, int, int, int *));
CRTDECL(int,      accept(int, struct sockaddr *, socklen_t *));
CRTDECL(int,      bind(int, const struct sockaddr *, socklen_t));
CRTDECL(int,      connect(int, const struct sockaddr *, socklen_t));
CRTDECL(int,      getpeername(int, struct sockaddr *, socklen_t *));
CRTDECL(int,      getsockname(int, struct sockaddr *, socklen_t *));
CRTDECL(int,      getsockopt(int, int, int, void *, socklen_t *));
CRTDECL(int,      setsockopt(int, int, int, const void *, socklen_t));
CRTDECL(int,      sockatmark(int));
CRTDECL(int,      listen(int, int));
CRTDECL(intmax_t, recv(int, void *, size_t, int));
CRTDECL(intmax_t, recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *));
CRTDECL(intmax_t, recvmsg(int, struct msghdr *, int));
CRTDECL(intmax_t, send(int, const void *, size_t, int));
CRTDECL(intmax_t, sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t));
CRTDECL(intmax_t, sendmsg(int, const struct msghdr *, int));
CRTDECL(int,      shutdown(int, int));

static inline struct sockaddr *
sstosa(struct sockaddr_storage *ss)
{
    return ((struct sockaddr *)(ss));
}
_CODE_END

#endif //!__INET_SOCKET_H__
