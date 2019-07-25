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
 * Ethernet Header Support Definitions & Structures
 * - This header describes the base ethernet-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __INET_ETHERNET_H__
#define	__INET_ETHERNET_H__

#include <stdint.h>

#define ETHER_ADDR_LEN  6

struct ether_addr {
  uint8_t ether_addr_octet[6];            /* 48-bit Ethernet address */
};

struct ether_header {
  uint8_t  ether_dhost[ETHER_ADDR_LEN];   /* Destination Ethernet address */
  uint8_t  ether_shost[ETHER_ADDR_LEN];   /* Source Ethernet address */
  uint16_t ether_type;                    /* Ethernet packet type*/
};

#endif //!__INET_ETHERNET_H__
