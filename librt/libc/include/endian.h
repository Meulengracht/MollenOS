/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * Standard C Library (Endian)
 *  - Contains definitions for the machine endianess
 */
#ifndef __STDC_ENDIAN__
#define __STDC_ENDIAN__

#define _LITTLE_ENDIAN      1234
#define _BIG_ENDIAN         4321
#define _PDP_ENDIAN         3412

#ifndef __BYTE_ORDER__
#define __ORDER_LITTLE_ENDIAN__  1234
#define __ORDER_BIG_ENDIAN__     4321
#endif

#if (defined(i386) || defined(__i386__)) || (defined(amd64) || defined(__amd64__))
#define _BYTE_ORDER         _LITTLE_ENDIAN
#define __BYTE_ORDER__      __ORDER_LITTLE_ENDIAN__
#else
#error "Please specify byte-order for this platform"
#endif

#define BYTE_ORDER          _BYTE_ORDER
#define LITTLE_ENDIAN       _LITTLE_ENDIAN
#define BIG_ENDIAN          _BIG_ENDIAN
#define PDP_ENDIAN          _PDP_ENDIAN

#ifndef __FLOAT_WORD_ORDER__
#define __FLOAT_WORD_ORDER__     __BYTE_ORDER__
#endif

#if _BYTE_ORDER == _LITTLE_ENDIAN
#define    _QUAD_HIGHWORD   1
#define    _QUAD_LOWWORD    0
#else
#define    _QUAD_HIGHWORD   0
#define    _QUAD_LOWWORD    1
#endif

#ifdef __GNUC__
#define    __bswap16(_x)    __builtin_bswap16(_x)
#define    __bswap32(_x)    __builtin_bswap32(_x)
#define    __bswap64(_x)    __builtin_bswap64(_x)
#else /* __GNUC__ */
static __inline __uint16_t
__bswap16(__uint16_t _x)
{
    return ((__uint16_t)((_x >> 8) | ((_x << 8) & 0xff00)));
}

static __inline __uint32_t
__bswap32(__uint32_t _x)
{
    return ((__uint32_t)((_x >> 24) | ((_x >> 8) & 0xff00) |
        ((_x << 8) & 0xff0000) | ((_x << 24) & 0xff000000)));
}

static __inline __uint64_t
__bswap64(__uint64_t _x)
{
    return ((__uint64_t)((_x >> 56) | ((_x >> 40) & 0xff00) |
        ((_x >> 24) & 0xff0000) | ((_x >> 8) & 0xff000000) |
        ((_x << 8) & ((__uint64_t)0xff << 32)) |
        ((_x << 24) & ((__uint64_t)0xff << 40)) |
        ((_x << 40) & ((__uint64_t)0xff << 48)) | ((_x << 56))));
}
#endif /* !__GNUC__ */

#ifndef __machine_host_to_from_network_defined
#if _BYTE_ORDER == _LITTLE_ENDIAN
#define    __htonl(_x)    __bswap32(_x)
#define    __htons(_x)    __bswap16(_x)
#define    __ntohl(_x)    __bswap32(_x)
#define    __ntohs(_x)    __bswap16(_x)
#else
#define    __htonl(_x)    ((__uint32_t)(_x))
#define    __htons(_x)    ((__uint16_t)(_x))
#define    __ntohl(_x)    ((__uint32_t)(_x))
#define    __ntohs(_x)    ((__uint16_t)(_x))
#endif
#endif /* __machine_host_to_from_network_defined */

#endif /* __MACHINE_ENDIAN_H__ */
