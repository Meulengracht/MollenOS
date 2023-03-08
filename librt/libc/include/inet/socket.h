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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Socket Support Definitions & Structures
 * - This header describes the base socket-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __INET_SOCKET_H__
#define	__INET_SOCKET_H__

#include <os/types/net.h>

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

// These are the valid values for the "how" field used by shutdown(2).
#define	SHUT_RD		0x1
#define	SHUT_WR		0x2
#define	SHUT_RDWR	(SHUT_RD | SHUT_WR)

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
