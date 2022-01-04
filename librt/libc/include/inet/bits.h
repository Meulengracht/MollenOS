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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Network Bits Definitions & Structures
 * - This header describes the base network bit-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
 
#ifndef __INET_BITS_H__
#define __INET_BITS_H__

#include <inttypes.h>
#include <endian.h>

#ifndef	_IN_PORT_T_DEFINED_
#define	_IN_PORT_T_DEFINED_
typedef	uint16_t in_port_t;
#endif

#ifndef	_IN_ADDR_T_DEFINED_
#define	_IN_ADDR_T_DEFINED_
typedef	uint32_t in_addr_t;
#endif

#define htonl(x) __htonl(x)
#define htons(x) __htons(x)
#define ntohl(x) __ntohl(x)
#define ntohs(x) __ntohs(x)

in_addr_t      inet_addr(const char *cp);
in_addr_t      inet_lnaof(struct in_addr in);
struct in_addr inet_makeaddr(in_addr_t net, in_addr_t lna);
in_addr_t      inet_netof(struct in_addr in);
in_addr_t      inet_network(const char *cp);
char*          inet_ntoa(struct in_addr in);

#endif //!__INET_BITS_H__
