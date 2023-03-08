/**
 * Copyright 2023, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TYPES_NET_H__
#define __TYPES_NET_H__

#include <os/osdefs.h>

/**
 * Socket types. These are defined to be compatable with the default
 * C networking types that posix implements.
 */
typedef	uint32_t socklen_t;
typedef	uint8_t  sa_family_t;

struct linger {
    // l_onoff is used to turn lingering on/off
    int	l_onoff;
    // l_linger is used to control the linger time
    int	l_linger;
};

struct sockaddr {
    // sa_len is the length of the socket address
    uint8_t sa_len;
    // sa_family is used to indicate which type of socket
    // address this describes
    sa_family_t sa_family;
    // sa_data is the data of the socket address, this is actually
    // longer than 13 bytes, and to *ensure* it can hold all addresses
    // space must be allocated to be enough for sizeof(sockaddr_storage).
    // This is only 13 bytes to align to 16 bytes
    char sa_data[14];
};

/**
 * RFC 2553: protocol-independent placeholder for socket addresses
 */
#define	_SS_MAXSIZE	128
#define	_SS_ALIGNSIZE	(sizeof(int64_t))
#define	_SS_PAD1SIZE	\
		(_SS_ALIGNSIZE - sizeof(uint8_t) - sizeof(sa_family_t))
#define	_SS_PAD2SIZE	\
		(_SS_MAXSIZE - sizeof(uint8_t) - sizeof(sa_family_t) - \
				_SS_PAD1SIZE - _SS_ALIGNSIZE)

// Note: __ss_{len,family} is defined in RFC2553.  During RFC2553 discussion
// the field name went back and forth between ss_len and __ss_len,
// and RFC2553 specifies it to be __ss_len.
struct sockaddr_storage {
    uint8_t       __ss_len;
    sa_family_t   __ss_family;
    unsigned char __ss_pad1[_SS_PAD1SIZE];
    uint64_t      __ss_pad2;
    unsigned char __ss_pad3[_SS_PAD2SIZE];
};

// Socket types
#define	SOCK_STREAM	    1   /* stream socket */
#define	SOCK_DGRAM      2   /* datagram socket */
#define	SOCK_RAW        3   /* datagram like raw-protocol interface */
#define	SOCK_RDM	    4   /* reliably-delivered message */
#define	SOCK_SEQPACKET	5	/* Connection based sequenced packet stream */

//Address families.
#define	AF_UNSPEC		0		/* unspecified */
#define	AF_UNIX			1		/* local to host */
#define	AF_LOCAL		AF_UNIX /* only to keep unix compatability */
#define	AF_INET			2		/* internetwork: UDP, TCP, etc. */
#define	AF_INET6		3		/* IPv6 */
#define	AF_BLUETOOTH	4		/* Bluetooth */
#define AF_MAX          5

// Protocol families, same as address families for now.
#define	PF_UNSPEC       AF_UNSPEC
#define	PF_LOCAL	    AF_LOCAL
#define	PF_UNIX		    AF_UNIX
#define	PF_INET		    AF_INET
#define PF_INET6	    AF_INET6
#define	PF_BLUETOOTH	AF_BLUETOOTH
#define	PF_MAX		    AF_MAX

struct iovec {
    void*  iov_base;    /* Starting address */
    size_t iov_len;     /* Number of bytes to transfer */
};

struct msghdr {
    void*         msg_name;         /* optional address */
    socklen_t     msg_namelen;      /* size of address */
    struct iovec* msg_iov;          /* scatter/gather array */
    unsigned int  msg_iovlen;       /* # elements in msg_iov */
    void*         msg_control;      /* ancillary data, see below */
    socklen_t     msg_controllen;   /* ancillary data buffer len */
    int           msg_flags;        /* flags on received message */
};

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

// Level number for (get/set)sockopt() to apply to socket itself.
#define	SOL_SOCKET	0xffff

// Maximum queue length specifiable by listen(2).
#define	SOMAXCONN	128

#endif //!__TYPES_NET_H__
