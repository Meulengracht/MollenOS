/* MollenOS
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
 * Event Queue Support Definitions & Structures
 * - This header describes the base event-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_SOCKET_H__
#define	__OS_SOCKET_H__

#ifndef	_SOCKLEN_T_DEFINED_
#define	_SOCKLEN_T_DEFINED_
typedef	__socklen_t	socklen_t;	/* length type for network syscalls */
#endif

#ifndef	_SA_FAMILY_T_DEFINED_
#define	_SA_FAMILY_T_DEFINED_
typedef	__sa_family_t sa_family_t;	/* sockaddr address family type */
#endif


// Socket types
#define	SOCK_STREAM	1		/* stream socket */
#define	SOCK_DGRAM	2		/* datagram socket */
#define	SOCK_RAW	3		/* raw-protocol interface */
#define	SOCK_RDM	4		/* reliably-delivered message */
#define	SOCK_SEQPACKET	5		/* sequenced packet stream */

// Option flags per-socket.
#define	SO_DEBUG	0x0001		/* turn on debugging info recording */
#define	SO_ACCEPTCONN	0x0002		/* socket has had listen() */
#define	SO_REUSEADDR	0x0004		/* allow local address reuse */
#define	SO_KEEPALIVE	0x0008		/* keep connections alive */
#define	SO_DONTROUTE	0x0010		/* just use interface addresses */
#define	SO_BROADCAST	0x0020		/* permit sending of broadcast msgs */
#define	SO_USELOOPBACK	0x0040		/* bypass hardware when possible */
#define	SO_LINGER	0x0080		/* linger on close if data present */
#define	SO_OOBINLINE	0x0100		/* leave received OOB data in line */
#define	SO_REUSEPORT	0x0200		/* allow local address & port reuse */
#define SO_TIMESTAMP	0x0800		/* timestamp received dgram traffic */
#define SO_BINDANY	0x1000		/* allow bind to any address */
#define SO_ZEROIZE	0x2000		/* zero out all mbufs sent over socket */

//Additional options, not kept in so_options.
#define	SO_SNDBUF	0x1001		/* send buffer size */
#define	SO_RCVBUF	0x1002		/* receive buffer size */
#define	SO_SNDLOWAT	0x1003		/* send low-water mark */
#define	SO_RCVLOWAT	0x1004		/* receive low-water mark */
#define	SO_SNDTIMEO	0x1005		/* send timeout */
#define	SO_RCVTIMEO	0x1006		/* receive timeout */
#define	SO_ERROR	0x1007		/* get error status and clear */
#define	SO_TYPE		0x1008		/* get socket type */
#define	SO_NETPROC	0x1020		/* multiplex; network processing */
#define	SO_RTABLE	0x1021		/* routing table to be used */
#define	SO_PEERCRED	0x1022		/* get connect-time credentials */
#define	SO_SPLICE	0x1023		/* splice data to other socket */

// Structure used for manipulating linger option.
struct	linger {
	int	l_onoff;		/* option on/off */
	int	l_linger;		/* linger time */
};

// Level number for (get/set)sockopt() to apply to socket itself.
#define	SOL_SOCKET	0xffff

//Address families.
#define	AF_UNSPEC	0		/* unspecified */
#define	AF_UNIX		1		/* local to host */
#define	AF_LOCAL	AF_UNIX		/* draft POSIX compatibility */
#define	AF_INET		2		/* internetwork: UDP, TCP, etc. */
#define	AF_IMPLINK	3		/* arpanet imp addresses */
#define	AF_PUP		4		/* pup protocols: e.g. BSP */
#define	AF_CHAOS	5		/* mit CHAOS protocols */
#define	AF_NS		6		/* XEROX NS protocols */
#define	AF_ISO		7		/* ISO protocols */
#define	AF_OSI		AF_ISO
#define	AF_ECMA		8		/* european computer manufacturers */
#define	AF_DATAKIT	9		/* datakit protocols */
#define	AF_CCITT	10		/* CCITT protocols, X.25 etc */
#define	AF_SNA		11		/* IBM SNA */
#define AF_DECnet	12		/* DECnet */
#define AF_DLI		13		/* DEC Direct data link interface */
#define AF_LAT		14		/* LAT */
#define	AF_HYLINK	15		/* NSC Hyperchannel */
#define	AF_APPLETALK	16		/* Apple Talk */
#define	AF_ROUTE	17		/* Internal Routing Protocol */
#define	AF_LINK		18		/* Link layer interface */
#define	pseudo_AF_XTP	19		/* eXpress Transfer Protocol (no AF) */
#define	AF_COIP		20		/* connection-oriented IP, aka ST II */
#define	AF_CNT		21		/* Computer Network Technology */
#define pseudo_AF_RTIP	22		/* Help Identify RTIP packets */
#define	AF_IPX		23		/* Novell Internet Protocol */
#define	AF_INET6	24		/* IPv6 */
#define pseudo_AF_PIP	25		/* Help Identify PIP packets */
#define AF_ISDN		26		/* Integrated Services Digital Network*/
#define AF_E164		AF_ISDN		/* CCITT E.164 recommendation */
#define AF_NATM		27		/* native ATM access */
#define	AF_ENCAP	28
#define	AF_SIP		29		/* Simple Internet Protocol */
#define AF_KEY		30
#define pseudo_AF_HDRCMPLT 31		/* Used by BPF to not rewrite headers
					   in interface output routine */
#define	AF_BLUETOOTH	32		/* Bluetooth */
#define AF_MPLS         33              /* MPLS */
#define pseudo_AF_PFLOW 34		/* pflow */
#define pseudo_AF_PIPEX 35		/* PIPEX */
#define AF_MAX          36

// Structure used to store most addresses.
struct sockaddr {
	__uint8_t   sa_len;		/* total length */
	sa_family_t sa_family;  /* address family */
	char        sa_data[14];/* actually longer; address value */
};

// Sockaddr type which can hold any sockaddr type available
// in the system.
// Note: __ss_{len,family} is defined in RFC2553.  During RFC2553 discussion
// the field name went back and forth between ss_len and __ss_len,
// and RFC2553 specifies it to be __ss_len.
struct sockaddr_storage {
	__uint8_t     __ss_len;		    /* total length */
	sa_family_t   __ss_family;	    /* address family */
	unsigned char __ss_pad1[6];	    /* align to quad */
	__uint64_t    __ss_pad2;	    /* force alignment for stupid compilers */
	unsigned char __ss_pad3[240];	/* pad to a total of 256 bytes */
};

// Protocol families, same as address families for now.
#define	PF_UNSPEC       AF_UNSPEC
#define	PF_LOCAL	    AF_LOCAL
#define	PF_UNIX		    AF_UNIX
#define	PF_INET		    AF_INET
#define	PF_IMPLINK	    AF_IMPLINK
#define	PF_PUP		    AF_PUP
#define	PF_CHAOS	    AF_CHAOS
#define	PF_NS		    AF_NS
#define	PF_ISO		    AF_ISO
#define	PF_OSI		    AF_ISO
#define	PF_ECMA		    AF_ECMA
#define	PF_DATAKIT	    AF_DATAKIT
#define	PF_CCITT	    AF_CCITT
#define	PF_SNA		    AF_SNA
#define PF_DECnet	    AF_DECnet
#define PF_DLI		    AF_DLI
#define PF_LAT		    AF_LAT
#define	PF_HYLINK	    AF_HYLINK
#define	PF_APPLETALK	AF_APPLETALK
#define	PF_ROUTE	    AF_ROUTE
#define	PF_LINK		    AF_LINK
#define	PF_XTP		    pseudo_AF_XTP	/* really just proto family, no AF */
#define	PF_COIP		    AF_COIP
#define	PF_CNT		    AF_CNT
#define	PF_IPX		    AF_IPX		/* same format as AF_NS */
#define PF_INET6	    AF_INET6
#define PF_RTIP		    pseudo_AF_RTIP	/* same format as AF_INET */
#define PF_PIP		    pseudo_AF_PIP
#define PF_ISDN		    AF_ISDN
#define PF_NATM		    AF_NATM
#define PF_ENCAP	    AF_ENCAP
#define	PF_SIP		    AF_SIP
#define PF_KEY		    AF_KEY
#define PF_BPF		    pseudo_AF_HDRCMPLT
#define	PF_BLUETOOTH	AF_BLUETOOTH
#define PF_MPLS		    AF_MPLS
#define PF_PFLOW	    pseudo_AF_PFLOW
#define PF_PIPEX	    pseudo_AF_PIPEX
#define	PF_MAX		    AF_MAX

// These are the valid values for the "how" field used by shutdown(2).
#define	SHUT_RD		0
#define	SHUT_WR		1
#define	SHUT_RDWR	2

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

//Header for ancillary data objects in msg_control buffer.
//Used for additional information with/about a datagram
//not expressible by flags.  The format is a sequence
//of message elements headed by cmsghdr structures.
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
int	accept(int, struct sockaddr *, socklen_t *);
int	bind(int, const struct sockaddr *, socklen_t);
int	connect(int, const struct sockaddr *, socklen_t);
int	getpeername(int, struct sockaddr *, socklen_t *);
int	getsockname(int, struct sockaddr *, socklen_t *);
int	getsockopt(int, int, int, void *, socklen_t *);
int	listen(int, int);
ssize_t	recv(int, void *, size_t, int);
ssize_t	recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
ssize_t	recvmsg(int, struct msghdr *, int);
ssize_t	send(int, const void *, size_t, int);
ssize_t	sendto(int, const void *,
	    size_t, int, const struct sockaddr *, socklen_t);
ssize_t	sendmsg(int, const struct msghdr *, int);
int	setsockopt(int, int, int, const void *, socklen_t);
int	shutdown(int, int);
int	sockatmark(int);
int	socket(int, int, int);
int	socketpair(int, int, int, int *);

static inline struct sockaddr *
sstosa(struct sockaddr_storage *ss)
{
	return ((struct sockaddr *)(ss));
}
_CODE_END

#endif //!__OS_SOCKET_H__
